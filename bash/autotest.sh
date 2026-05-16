#!/usr/bin/env bash
# =============================================================================
# autotest.sh — one-shot local verification gate for Sirius Looper on macOS.
#
# Runs every gate in order and bails on the first failure with a clear
# "PHASE X FAILED" prefix so the operator knows where to look. Phases:
#
#   1. Headless unit tests       — fast dev-loop build (build/), ctest
#   2. Signed Xcode bundle build — build-xcode/, Developer ID signing
#   3. Signing verification      — codesign --verify, spctl
#   4. GUI smoke                 — bash/smoke-persistence.sh against signed
#
# Each phase prints its wall-clock duration. Total runtime: O(few minutes)
# on Apple Silicon dev machines (most time in phase 2 — Xcode generator is
# noticeably slower than Unix Makefiles).
#
# Notarization is NOT run here — it's network-bound, slow, throttled by
# Apple, and only needed for release artefacts. Add a separate
# autotest-release.sh or a --notarize flag if/when needed.
#
# Linux/Windows are NOT exercised — per the macOS-first project rule,
# this script is macOS-only. Cross-platform CI is its own concern.
#
# Exit codes:
#   0 — all phases passed
#   1 — phase 1 (headless tests) failed
#   2 — phase 2 (signed build) failed
#   3 — phase 3 (signing verification) failed
#   4 — phase 4 (GUI smoke) failed
# =============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

APP_BUNDLE="$ROOT/build-xcode/app/SiriusLooper_artefacts/Release/Sirius Looper.app"

# --- timing helper -------------------------------------------------------
phase_start() {
    PHASE_NAME="$1"
    PHASE_START_TS=$(date +%s)
    echo
    echo "================================================================"
    echo "  $PHASE_NAME"
    echo "================================================================"
}

phase_end() {
    local elapsed=$(( $(date +%s) - PHASE_START_TS ))
    echo "  ✓ $PHASE_NAME — ${elapsed}s"
}

fail() {
    local code="$1"
    local msg="$2"
    echo
    echo "================================================================"
    echo "  PHASE $code FAILED: $msg"
    echo "================================================================"
    exit "$code"
}

# --- Phase 1: headless unit tests ---------------------------------------
phase_start "Phase 1: headless unit tests (build/, ctest)"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null \
    || fail 1 "cmake configure (build/) failed"
cmake --build build --config Release --target SiriusTests --parallel \
    || fail 1 "SiriusTests build failed"
ctest --test-dir build --build-config Release --output-on-failure \
    || fail 1 "ctest reported failures"
phase_end "Phase 1: headless unit tests"

# --- Phase 2: signed Xcode bundle build ---------------------------------
phase_start "Phase 2: signed Xcode bundle (build-xcode/, Developer ID)"
cmake -B build-xcode -G Xcode >/dev/null \
    || fail 2 "cmake configure (build-xcode/) failed"
cmake --build build-xcode --config Release --target SiriusLooper \
    || fail 2 "SiriusLooper signed build failed"
[[ -d "$APP_BUNDLE" ]] \
    || fail 2 "expected bundle not produced at $APP_BUNDLE"
phase_end "Phase 2: signed Xcode bundle"

# --- Phase 3: signing verification --------------------------------------
phase_start "Phase 3: signing verification (codesign, spctl)"
codesign --verify --deep --strict "$APP_BUNDLE" \
    || fail 3 "codesign --verify rejected the bundle"
# Snapshot codesign output once and grep the captured text. Calling
# `codesign -dv` repeatedly back-to-back has produced flaky pipeline
# results in this script (the second invocation occasionally appears to
# read a transient state, surfaced as a missing TeamIdentifier or flags
# line even though the bundle is correctly signed). One read + many
# greps is both faster and reliable.
CS_OUT="$(codesign -dvv "$APP_BUNDLE" 2>&1)"
grep -q "TeamIdentifier=RR5DY39W4Q" <<<"$CS_OUT" \
    || fail 3 "TeamIdentifier RR5DY39W4Q not present in signature:
$CS_OUT"
grep -q "flags=0x10000(runtime)" <<<"$CS_OUT" \
    || fail 3 "hardened runtime flag missing from signature:
$CS_OUT"
grep -q "Authority=Developer ID Application: Larry Seyer (RR5DY39W4Q)" <<<"$CS_OUT" \
    || fail 3 "Developer ID Application authority not in signing cert chain:
$CS_OUT"
# spctl exits non-zero if it rejects the bundle, so the grep is belt-and-suspenders.
spctl -a -t exec -vv "$APP_BUNDLE" 2>&1 | grep -q "source=Developer ID\|source=Notarized Developer ID" \
    || fail 3 "spctl did not accept bundle as Developer ID"
phase_end "Phase 3: signing verification"

# --- Phase 4: GUI smoke -------------------------------------------------
phase_start "Phase 4: GUI smoke (smoke-persistence.sh)"
killall "Sirius Looper" 2>/dev/null || true
sleep 1
APP_BUNDLE="$APP_BUNDLE" bash "$ROOT/bash/smoke-persistence.sh" \
    || fail 4 "smoke-persistence.sh reported failure (see exit-code triage in continue.md §4)"
phase_end "Phase 4: GUI smoke"

# --- Summary ------------------------------------------------------------
echo
echo "================================================================"
echo "  ✓ autotest passed — all 4 phases green"
echo "================================================================"
exit 0
