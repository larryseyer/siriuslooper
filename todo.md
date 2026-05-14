# Sirius Looper — Deferred Items

### 2026-05-14 — M0: Project skeleton & CI

- **Files:** n/a (external/operator actions)
- **What was deferred:**
  1. Ableton Link proprietary license procurement (email link-devs@ableton.com).
  2. Throwaway FFmpeg-integration spike to de-risk M6 video.
  3. Operator verification that the standalone window launches and renders
     (build produces a valid `Sirius Looper.app` bundle; the window itself was
     not launched in this environment).
  4. CI workflow (`.github/workflows/ci.yml`) is committed but unverified — it
     cannot run until the repo has a GitHub remote and a push.
- **Why deferred:**
  1. Business/legal action, not a code task — cannot be performed here.
  2. Exploratory; requires FFmpeg installed locally and is M6-scoped risk
     reduction, not M0-blocking.
  3. GUI testing is operator-run per project conventions.
  4. No remote configured; pushing is an explicit operator decision.
- **What's needed to finish:**
  1. Larry emails Ableton, secures the license agreement before M8 (clock layer
     already designed to treat Link as one pluggable discipline source).
  2. Install FFmpeg, write a small decode-one-frame probe, confirm libav links
     cleanly via CMake on all three platforms. Do before/early in M6.
  3. Launch `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app` and
     confirm the window opens, is resizable, and shows "Sirius Looper".
  4. Add a GitHub remote and push; confirm the CI matrix goes green on
     macOS/Windows/Linux.
