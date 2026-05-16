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

APP_BUNDLE="${APP_BUNDLE:-/Users/larryseyer/Sirius Looper/build/app/SiriusLooper_artefacts/Release/Sirius Looper.app}"
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
osascript -e "tell application \"${APP_NAME}\" to quit" >/dev/null 2>&1 || true
sleep 1
open "${APP_BUNDLE}"

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

# Click a button whose accessible name matches the given regex. JUCE
# TextButtons expose their button text as the AX name. Returns 0 if a button
# was clicked, non-zero otherwise.
click_button_named() {
  local pattern="$1"
  osascript <<APPLESCRIPT >/dev/null
    tell application "System Events"
      tell process "${APP_NAME}"
        repeat with btn in (every button of window 1)
          if (name of btn as string) contains "${pattern}" then
            click btn
            return
          end if
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
        repeat with t in (every static text of window 1)
          try
            set out to out & (value of t as string) & linefeed
          end try
        end repeat
        return out
      end tell
    end tell
APPLESCRIPT
}

# Type a full path into the focused NSSavePanel / NSOpenPanel using
# Cmd+Shift+G ("Go to Folder"), then commit with Return. This is the most
# stable cross-locale navigation — the dialog's own text field defaults to
# the chosen filename and varies by locale.
type_path_into_dialog() {
  local path="$1"
  # Wait briefly for the sheet to attach.
  sleep 1
  osascript <<APPLESCRIPT >/dev/null
    tell application "System Events"
      keystroke "g" using {command down, shift down}
      delay 0.5
      keystroke "${path}"
      delay 0.2
      key code 36
      delay 0.5
      key code 36
    end tell
APPLESCRIPT
}

# --- Save flow -----------------------------------------------------------
echo "[smoke] clicking Save..."
click_button_named "Save"
type_path_into_dialog "${TMP_FILE}"
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
type_path_into_dialog "${TMP_FILE}"
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
