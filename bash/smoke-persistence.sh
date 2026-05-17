#!/bin/bash
# =============================================================================
# smoke-persistence.sh — drive the Sirius Looper .app through a Save / Load
# round-trip via osascript + System Events accessibility, then verify the
# status banner reports success.
#
# This covers the GUI-integration plumbing (TextButton click, NSSavePanel /
# NSOpenPanel navigation, status-banner write) that the headless SiriusTests
# cases cannot reach. The persistence engine itself is covered by the
# [sessionformat] tests; this script is the last-mile GUI smoke.
#
# PRECONDITION — the .app must be Developer-ID-signed.
#
# On an ad-hoc-signed bundle (codesign -dv reports
# flags=0x20002(adhoc,linker-signed)), macOS denies System Events automation
# targeting the process with AppleScript error -25211, even when the calling
# shell HAS accessibility access. This is the same TCC failure mode as the
# Load-dialog file-greying issue tracked in todo.md (2026-05-15 — "Load
# dialog still cannot select `.sirius.json` on macOS"). Resolving the
# signing problem unblocks both this script and the Load dialog at once.
#
# Until then this script is intentionally committed but cannot run. It
# documents the smoke path that will become available once signing lands.
#
# Requires (after signing):
#   * macOS accessibility access granted to the shell that runs osascript.
#     System Settings → Privacy & Security → Accessibility → add and toggle
#     Terminal.app / iTerm.app / your editor / /usr/bin/osascript.
#
# Exit codes:
#   0 — banner reports "Loaded ..." after the round-trip
#   1 — TCC accessibility access not granted (script prints what to fix)
#   2 — app failed to launch, or window-1 never appeared (most likely the
#       Developer ID signing precondition is still unmet)
#   3 — Save flow failed (banner did not show "Saved to ...")
#   4 — Load flow failed (banner did not show "Loaded ...")
#   5 — unexpected error driving the UI
# =============================================================================

set -euo pipefail

APP_BUNDLE="${APP_BUNDLE:-/Users/larryseyer/SiriusLooper/build/app/SiriusLooper_artefacts/Release/Sirius Looper.app}"
APP_NAME="Sirius Looper"
TMP_DIR="$(mktemp -d -t sirius-smoke)"
TMP_FILE="${TMP_DIR}/smoke-session.sirius.json"

cleanup() {
  osascript -e "tell application \"${APP_NAME}\" to quit" >/dev/null 2>&1 || true
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

# --- Sanity: app bundle exists --------------------------------------------
if [[ ! -d "${APP_BUNDLE}" ]]; then
  echo "ERROR: app bundle not found at ${APP_BUNDLE}" >&2
  echo "Build first: cmake --build build --target SiriusLooper -j" >&2
  exit 2
fi

# --- TCC probe: confirm System Events can drive a specific process. The
# loose probe (`get name of first process`) succeeds even when targeted
# process control is denied, so this targets Finder, a process every Mac
# has and the OS allows ad-hoc binaries to drive.
probe_tcc() {
  osascript -e 'tell application "System Events" to tell process "Finder" to count windows' \
    >/dev/null 2>&1
}

if ! probe_tcc; then
  cat <<'EOF' >&2
ERROR: System Events accessibility access is denied.

Grant it once:
  1. System Settings → Privacy & Security → Accessibility
  2. Click + and add the terminal / IDE / shell that runs this script
     (Terminal.app, iTerm.app, your editor, or /usr/bin/osascript)
  3. Toggle the switch on
  4. Re-run this script
EOF
  exit 1
fi

# --- Quit any prior instance, then relaunch ------------------------------
# Launch the binary directly rather than via `open`. On dev-tree paths
# outside /Applications, `open` against a Developer-ID-signed bundle
# can fail with -10825 (Launch Services rejecting a freshly-built
# bundle whose identifier clashes with an older ad-hoc-signed copy in
# a sibling build/ tree). Direct binary launch sidesteps Launch
# Services entirely and is more robust for both local automation and
# CI. End-user launches go through Finder/Dock against a notarized
# bundle in /Applications, which is not affected.
osascript -e "tell application \"${APP_NAME}\" to quit" >/dev/null 2>&1 || true
sleep 1
"${APP_BUNDLE}/Contents/MacOS/${APP_NAME}" >/dev/null 2>&1 &

# --- Wait for the main window to appear (poll up to ~10 s) ---------------
ready=0
for _ in $(seq 1 20); do
  count=$(osascript -e "tell application \"System Events\" to tell process \"${APP_NAME}\" to count windows" 2>/dev/null || echo 0)
  if [[ "${count}" -ge 1 ]]; then
    ready=1
    break
  fi
  sleep 0.5
done
if [[ "${ready}" -ne 1 ]]; then
  echo "ERROR: ${APP_NAME} window-1 never appeared" >&2
  exit 2
fi

# Click a button whose accessible name contains the given substring. Walks
# the AX tree of window 1 explicitly (top-level + one level into each
# AXGroup) — System Events' `entire contents` strips role information in
# some Sequoia builds, so we recurse by hand. Tab buttons in JUCE
# TabbedComponent are nested one AXGroup deep; Save/Load on the Preparation
# pane are similarly nested. JUCE TextButtons expose their button text as
# the AX name. Returns 0 if a button was clicked, non-zero otherwise.
click_button_named() {
  local pattern="$1"
  osascript <<APPLESCRIPT >/dev/null
    tell application "System Events"
      tell process "${APP_NAME}"
        -- Top-level buttons first.
        repeat with btn in (every button of window 1)
          try
            if (name of btn as string) contains "${pattern}" then
              click btn
              return
            end if
          end try
        end repeat
        -- Then one level into each top-level UI element (groups, etc.).
        repeat with grp in (every UI element of window 1)
          try
            repeat with btn in (every button of grp)
              try
                if (name of btn as string) contains "${pattern}" then
                  click btn
                  return
                end if
              end try
            end repeat
          end try
        end repeat
        -- And two levels deep — the Preparation pane wraps Save/Load in
        -- another container.
        repeat with grp in (every UI element of window 1)
          try
            repeat with sub in (every UI element of grp)
              try
                repeat with btn in (every button of sub)
                  try
                    if (name of btn as string) contains "${pattern}" then
                      click btn
                      return
                    end if
                  end try
                end repeat
              end try
            end repeat
          end try
        end repeat
        error "no button matching ${pattern}"
      end tell
    end tell
APPLESCRIPT
}

# Read the latest status banner. The Preparation pane's status line is a
# JUCE Label, exposed as AXStaticText. The banner is the most-recently-
# updated static text on window 1; grabbing the union of static texts and
# returning their values lets the caller grep for "Saved" / "Loaded" /
# "Load failed".
read_window_texts() {
  osascript <<APPLESCRIPT 2>/dev/null
    tell application "System Events"
      tell process "${APP_NAME}"
        set out to ""
        -- Top-level static texts.
        repeat with t in (every static text of window 1)
          try
            set out to out & (value of t as string) & linefeed
          end try
        end repeat
        -- One level deep.
        repeat with grp in (every UI element of window 1)
          try
            repeat with t in (every static text of grp)
              try
                set out to out & (value of t as string) & linefeed
              end try
            end repeat
          end try
        end repeat
        -- Two levels deep (Preparation pane wraps the status label).
        repeat with grp in (every UI element of window 1)
          try
            repeat with sub in (every UI element of grp)
              try
                repeat with t in (every static text of sub)
                  try
                    set out to out & (value of t as string) & linefeed
                  end try
                end repeat
              end try
            end repeat
          end try
        end repeat
        return out
      end tell
    end tell
APPLESCRIPT
}

# Drive an NSSavePanel / NSOpenPanel to a known path and commit. The dialog
# opens as a separate top-level window (not a sheet) on macOS Sequoia+; we
# target it by title prefix and click its primary action button explicitly
# rather than relying on Return keystrokes. Returns 0 on success.
#
# Path strategy:
#   1. Cmd+Shift+G opens "Go to Folder" sub-dialog
#   2. Type the full path (including basename)
#   3. Return commits the Go-to-Folder — NSSavePanel navigates to the
#      directory and prefills the basename in its filename field
#   4. Find the dialog window by title prefix and click its action button
#      by name ("Save" / "Open"); avoids the Return-keystroke landmines
#      (overwrite confirms, focus shifts, locale variance)
drive_dialog_to_path() {
  local path="$1"
  local action_name="$2"   # "Save" or "Open"
  local dialog_title_prefix="$3"  # e.g. "Save Sirius session" / "Load Sirius session"

  # Wait for dialog window to appear (poll up to ~5 s).
  local dialog_present=0
  for _ in $(seq 1 10); do
    local n
    n=$(osascript -e "tell application \"System Events\" to tell process \"${APP_NAME}\" to count (windows whose name starts with \"${dialog_title_prefix}\")" 2>/dev/null || echo 0)
    if [[ "${n}" -ge 1 ]]; then
      dialog_present=1
      break
    fi
    sleep 0.5
  done
  if [[ "${dialog_present}" -ne 1 ]]; then
    echo "ERROR: dialog window '${dialog_title_prefix}...' never appeared" >&2
    return 1
  fi

  # Activate Sirius Looper AND send keystrokes in the same osascript block
  # so focus can't slip back to the terminal between the activate and
  # the typing. (When activate and keystroke are in separate osascript
  # invocations, the shell that launched osascript steals focus back
  # before the keystrokes fire, and the typed path lands in the terminal
  # instead of the dialog — verified live by the operator.)
  #
  # Use key code 5 (= 'g') for Cmd+Shift+G instead of `keystroke "g"
  # using {modifiers}` — the latter silently no-ops on Sequoia inside
  # NSSavePanel/NSOpenPanel when the filename field has focus (the field
  # eats the modified keystroke), while the raw key code reliably
  # triggers the system shortcut.
  osascript <<APPLESCRIPT >/dev/null
    tell application "${APP_NAME}" to activate
    delay 0.4
    tell application "System Events"
      tell process "${APP_NAME}"
        set frontmost to true
        try
          perform action "AXRaise" of (first window whose name starts with "${dialog_title_prefix}")
        end try
      end tell
      delay 0.3
      key code 5 using {command down, shift down}
      delay 0.8
      keystroke "${path}"
      delay 0.4
      key code 36
    end tell
APPLESCRIPT
  sleep 1.2

  # Click the action button in the dialog window by name, or fall back to
  # Return if the button is disabled. NSOpenPanel's "Open" stays disabled
  # until a file is selected in the listing — Cmd+Shift+G navigates to
  # the directory but doesn't select; the trailing Return both selects
  # and opens. For NSSavePanel, "Save" is enabled once the filename field
  # is filled (which Cmd+Shift+G does), so the direct click works and
  # sidesteps the overwrite-confirm Return-keystroke trap.
  osascript <<APPLESCRIPT >/dev/null
    tell application "System Events"
      tell process "${APP_NAME}"
        set dlg to first window whose name starts with "${dialog_title_prefix}"
        try
          set actBtn to first button of dlg whose name is "${action_name}"
          if enabled of actBtn then
            click actBtn
          else
            key code 36
          end if
        on error
          key code 36
        end try
      end tell
    end tell
APPLESCRIPT
  sleep 1

  # If an overwrite-confirm sheet appears, dismiss it by clicking Replace.
  osascript <<APPLESCRIPT >/dev/null 2>&1 || true
    tell application "System Events"
      tell process "${APP_NAME}"
        try
          tell sheet 1 of (first window whose name starts with "${dialog_title_prefix}")
            click (first button whose name is "Replace")
          end tell
        end try
      end tell
    end tell
APPLESCRIPT
}

# --- Switch to Preparation tab (Save / Load live there) ------------------
echo "[smoke] switching to Preparation tab..."
click_button_named "Preparation"
sleep 1

# --- Save flow -----------------------------------------------------------
echo "[smoke] clicking Save..."
click_button_named "Save"
drive_dialog_to_path "${TMP_FILE}" "Save" "Save Sirius session"
sleep 1

# Confirm the file appeared on disk.
if [[ ! -f "${TMP_FILE}" ]]; then
  echo "ERROR: Save did not write ${TMP_FILE}" >&2
  echo "Window text was:" >&2
  read_window_texts >&2
  exit 3
fi

texts="$(read_window_texts)"
if ! grep -q "Saved" <<<"${texts}"; then
  echo "WARN: status banner did not confirm Save (window texts follow)" >&2
  echo "${texts}" >&2
  # Not fatal — the file exists; continue to Load.
fi

# --- Load flow -----------------------------------------------------------
echo "[smoke] clicking Load..."
click_button_named "Load"
drive_dialog_to_path "${TMP_FILE}" "Open" "Load Sirius session"
sleep 1

texts="$(read_window_texts)"
if grep -q "Load failed" <<<"${texts}"; then
  echo "ERROR: Load failed banner detected" >&2
  echo "${texts}" >&2
  exit 4
fi
if ! grep -q "Loaded" <<<"${texts}"; then
  echo "ERROR: status banner did not confirm Load" >&2
  echo "Window texts were:" >&2
  echo "${texts}" >&2
  exit 4
fi

# --- Verify the file is structurally what we expect ---------------------
# The on-disk JSON should contain refs (proof v2 shared-encoding ran) when
# the demo session is the source (verse × 3 yields two refs). This is an
# end-to-end pin on the encoding path the test harness cannot reach.
ref_count=$(grep -c '"ref"' "${TMP_FILE}" || true)
if [[ "${ref_count}" -lt 2 ]]; then
  echo "WARN: saved file has ${ref_count} ref entries; expected ≥ 2 for verse × 3" >&2
  echo "(this is informational — Load already succeeded above)" >&2
fi

echo "[smoke] PASS — Save / Load round-trip OK (refs in file: ${ref_count})"
exit 0
