# Sirius Looper — Deferred Items

### 2026-05-14 — M0: Project skeleton & CI

- **Files:** n/a (external/operator actions)
- **What was deferred:**
  1. Throwaway FFmpeg-integration spike to de-risk M6 video.
  2. Operator verification that the standalone window launches and renders
     (build produces a valid `Sirius Looper.app` bundle; the window itself was
     not launched in this environment).
  3. CI workflow (`.github/workflows/ci.yml`) is committed but unverified — it
     cannot run until the repo has a GitHub remote and a push.
- **Why deferred:**
  1. Exploratory; requires FFmpeg installed locally and is M6-scoped risk
     reduction, not M0-blocking.
  2. GUI testing is operator-run per project conventions.
  3. No remote configured; pushing is an explicit operator decision.
- **What's needed to finish:**
  1. Install FFmpeg, write a small decode-one-frame probe, confirm libav links
     cleanly via CMake on all three platforms. Do before/early in M6.
  2. Launch `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app` and
     confirm the window opens, is resizable, and shows "Sirius Looper".
  3. Add a GitHub remote and push; confirm the CI matrix goes green on
     macOS/Windows/Linux.

**Resolved:** Ableton Link license procurement — the proprietary license is
already held (same licensing model as the sister app OTTO; see
`LICENSE-THIRD-PARTY.md`). No procurement action is outstanding.

### 2026-05-14 — M2: Membranes & always-running tape

- **Files:** future `engine/` membrane classes + `app/AudioDeviceManagement.*`
- **What was deferred (the hardware-dependent half of M2):**
  1. JUCE `AudioIODeviceCallback` device wiring — the thin glue that drives the
     inbound membrane (timestamp + enqueue to the lock-free queue) and the
     outbound membrane (render the loop) from a real audio device.
  2. One-time loopback latency calibration per device (output a click, capture
     it back, measure the round-trip).
  3. The end-to-end "audio in → tape → mark a loop → hear it repeat" milestone
     test.
- **Why deferred:** all three require real audio hardware; device and audio
  testing is operator-run per project conventions, not verifiable headless.
- **What's needed to finish:**
  1. Build the membrane device-callback classes on top of the verified engine
     foundation (lock-free queue, LMC, membrane math, LoopRenderer, Asrc), then
     wire them through JUCE's `AudioDeviceManager` in `app/`.
  2. Run the loopback calibration on a real interface; confirm it populates an
     `AudioDeviceCalibration` with sane rate-factor/offset values.
  3. With an instrument plugged in: capture audio, mark a loop, confirm it
     repeats audibly and stays in time over many cycles.

- **Headless verification already done for M2:** lock-free SPSC queue
  (incl. 200k-item concurrent stress), retroactive ring, LMC, latency-
  compensation math, device calibration, LoopRenderer, and the libsoxr ASRC —
  78 tests pass. The libsoxr variable-rate latency measurement the plan asked
  for: reported delay ~2.1 ms, impulse-response latency ~0.04 ms — both
  comfortably inside the <30 ms trust budget.

### 2026-05-14 — M3: Minimal functional UI

- **Files:** `app/SessionInspector.{h,cpp}`, `app/DemoSession.{h,cpp}`,
  `app/Main.cpp`
- **What was deferred:**
  1. Operator verification that the session-inspector window launches: the
     Constituent tree renders as an indented hierarchy, the playhead slider
     scrubs, and the "loops sounding" panel updates as it moves.
- **Why deferred:** GUI testing is operator-run per project conventions; the
  build produces a valid `Sirius Looper.app` bundle but the window was not
  launched in this environment.
- **What's needed to finish:**
  1. Launch `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app`.
     Confirm: the demo tree shows the session with intro/verse/outro phrases
     (verse holding two layered loops); dragging the playhead from 0 to 24 s
     moves the active-reads panel through intro -> verse (2 loops) -> outro;
     the read positions advance monotonically within each phrase.

- **Headless verification already done for M3 UI:** the inspector is built on
  the same RenderPipeline exercised by 105 passing unit tests (incl. the
  arrangement integration test that pins down sequenced end-to-end playback);
  the demo tree is constructed with the verified `arrangement::sequence` /
  `arrangement::layer` primitives. The UI is a pure view over verified core +
  engine code — no new untested logic, only JUCE rendering of it.
