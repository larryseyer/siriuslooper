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

### 2026-05-14 — M5: Plugin hosting milestone test

- **Files:** `host/PluginScanner.{h,cpp}`, `host/GenericParameterView.{h,cpp}`,
  and the app integration that hosts a scanned plugin's processor and shows
  its parameter view.
- **What was deferred:**
  1. The plan's milestone-test commitment: "scan and instantiate at least one
     real plugin of each supported format (VST3 on every platform, AudioUnit
     on macOS); confirm parameter automation round-trips through a parameter
     tape."
  2. Wiring the GenericParameterView into the app's SessionInspector (or a
     successor panel) so the operator can actually drive a hosted plugin and
     see/edit its parameters.
  3. Optional CLAP and AUv3 hosting — explicitly gated as best-effort by the
     plan; not in any milestone gate.
- **Why deferred:** every part requires real plugin binaries installed on the
  test machine and a real-audio path; per project conventions hardware/GUI
  testing is operator-run.
- **What's needed to finish:**
  1. Have real plugins installed (at least one VST3; on macOS at least one
     AudioUnit). Run a PluginScanner, confirm both formats appear in
     `descriptors` with sensible names/manufacturers and no entries in
     `failedFiles`.
  2. Instantiate one via JUCE's `AudioPluginFormatManager::createPluginInstance`
     using the descriptor's `uniqueId` and `filePath`, attach the resulting
     `AudioProcessor` to a GenericParameterView in a small host harness,
     confirm sliders match the plugin's parameter count and respond to drag.
  3. With the plugin running through the audio path, move a parameter slider
     while recording onto a `Tape<ParameterEvent>` and play back; confirm the
     replayed events drive the plugin parameter and the view reflects the
     replay.

- **Headless verification already done for M5:** PluginDescriptor and
  EffectChain (copy-on-write, persistence round-trip, exhaustive variant
  coverage), ParameterEvent (range invariant, tape append-forward, the
  Constituent-over-parameter-tape recursion), PluginScanner's format
  registration (VST3 always; AudioUnit on macOS), and GenericParameterView's
  construction against a JUCE `AudioProcessor` — 146 tests pass. The
  hosted-plugin runtime that connects them is the operator-verified half.

### 2026-05-14 — M7: The performer's instrument — operator verification

- **Files:** `ui/PerformanceView.{h,cpp}`, `ui/PreparationView.{h,cpp}`, the
  app integration that drives them.
- **What was deferred:**
  1. Wiring `PerformanceView` and `PreparationView` into the standalone app
     in place of `SessionInspector`, and switching between cognitive modes
     (Performance vs Preparation, white paper 14.4) with a single gesture.
  2. Driving `UndoStack` with real edit operations — bind a global
     keyboard/footswitch undo and redo, plumb it through every editing path,
     and confirm a multi-step session of edits undoes and redoes cleanly.
  3. Integrating `LatencyBudget` with the UI's frame-loop and announcing
     when the rolling window falls below the budget (white paper 13.3
     rule 3, 14.8). The budget is measurable; the announcement surface
     belongs to the running app.
- **Why deferred:** every part requires the operator at the screen and a
  real gesture loop — eyes-free operation, glance tests, and trust-budget
  feel are all human-perceptual qualities.
- **What's needed to finish:**
  1. Add a Performance/Preparation toggle to the main window; route the
     same `Constituent` root to `selectPerformanceView` and
     `selectPreparationView` on every update.
  2. Hook the undo stack into every Constituent-producing edit path, push
     a label per edit, and confirm undo/redo land on the right snapshots.
  3. Measure UI-event-to-paint latency, feed each frame to
     `LatencyBudget`, and surface the rolling `fractionWithinBudget` in
     the Preparation view's diagnostics row.

- **Headless verification already done for M7:** UndoStack (push/undo/redo,
  redo-branch invalidation, depth cap, null guards), LatencyBudget (band
  thresholds, rolling window, invalid input rejected), the Performance and
  Preparation view-state selectors (deepest named container wins as
  foreground phrase, cycle-status formatting for the three cardinalities,
  full depth-first row enumeration with effect-chain and role-fillable
  flags) — 163 tests pass. The JUCE Components are thin renderers over
  those verified state structs.

### 2026-05-14 — M8: Ensemble — operator verification

- **Files:** `net/LmcElection.{h,cpp}`, `net/SessionMerge.{h,cpp}`,
  `net/Transport.h`, plus the (currently unwritten) network transport that
  carries the message types over real sockets.
- **What was deferred:**
  1. The plan's milestone test: "two-node partition-and-rejoin test
     confirming clean CRDT merge with no audio loss" — requires two real
     machines, a real network, and a real way to induce and lift a
     partition.
  2. The transport implementation itself: real sockets, frame format,
     reliability, discovery, monitoring previews. None of this exists yet;
     the message data types are written and tested but no wire goes
     anywhere.
  3. Per-node interval *measurement* feeding `LmcElection`. The election
     consumes intervals; the platform-specific code that *produces* an
     interval from each node's clock-discipline source (GPS, NTP, audio
     word clock, quartz, software estimate) is the operator-side
     contribution.
- **Why deferred:** every part requires real networking and real clocks.
  Per project conventions, hardware-dependent and network-dependent work
  is operator-run.
- **What's needed to finish:**
  1. Pick a transport (the plan does not commit to one; OSC over UDP and
     Ableton Link's own discovery layer are plausible candidates).
     Implement send/receive against `EnsembleMessage` with a small
     reliable-multicast or per-message-type semantics.
  2. Wire each node's clock-discipline source through to a
     `NodeClockEstimate`. Run `electLmc` on every topology change. Honour
     the result — slaves discipline against the master's broadcast time
     announcements.
  3. Use `MergeableSession::merge` on rejoin. The two-node milestone test:
     start with a shared session, partition the network, have each node
     edit different (and the same) Constituents, rejoin, merge, confirm
     no audio loss and no rewritten tape data.

- **Headless verification already done for M8:** LmcElection (anchor
  override beats tier, tier dominance beats numerical majority, Marzullo
  discards falsetickers and picks the narrowest-interval master),
  SessionMerge (tape-hash union, version union, commutativity,
  associativity, idempotence, last-writer-wins on the active version), and
  the `EnsembleMessage` variant (each kind constructs and pattern-matches
  cleanly) — 178 tests pass total across the whole project.

### 2026-05-14 — M6: Video — operator verification

- **Files:** `video/VideoFrame.h`, `video/VideoTape.h`,
  `video/FrameMembrane.{h,cpp}`, `video/VideoPreview.{h,cpp}`, and the
  (currently unwritten) FFmpeg decode/encode pipeline that produces and
  consumes the bytes the data model stores.
- **What was deferred:**
  1. The FFmpeg spike still owed from M0: install FFmpeg locally, write a
     small decode-one-frame probe, confirm libav* links cleanly via CMake
     on macOS, Windows, and Linux. **M6's runtime cannot proceed without
     this** — the data model is complete; the bytes are missing.
  2. The decode pipeline that fills `VideoFrame::pixels` from a real video
     source (camera input, file playback) and writes the metadata the tape
     and membrane consume. Real plumbing — choose libav* directly or wrap
     it behind a small abstraction layer.
  3. The encode pipeline that takes a captured `VideoTape` and writes it
     to disk in an intra-frame codec (white paper Part 6.5: roughly half
     the storage cost of uncompressed, decode cost paid at read).
  4. The conversion from a `VideoFrame` payload (any of the five pixel
     formats) to a `juce::Image` for `VideoPreview::setFrame`. swscale or
     equivalent does this trivially once linked.
  5. The plan's milestone test: "frame-accurate playback test against a
     known video file; confirm audio/video stay LMC-locked over a
     multi-minute render."
- **Why deferred:** every part requires FFmpeg installed locally and real
  source material. Per project conventions, anything with a real
  hardware/codec dependency is operator-run.
- **What's needed to finish:**
  1. Run the FFmpeg spike. Confirm linkage on the three platforms; pick a
     decode entry point (probably `avformat_open_input` +
     `avcodec_send_packet` / `avcodec_receive_frame`) and a swscale path
     for pixel-format conversion to the five `VideoPixelFormat` values
     the data model commits to.
  2. Wire decoded frames onto a `VideoTape`: each `av_frame` becomes a
     `VideoFrame` with width/height/format from the av_frame and a
     `presentationLmcSeconds` computed from the av_frame's pts and the
     stream's time_base. Append in order; the tape's
     "non-decreasing LMC time" rule enforces correctness.
  3. Drive `VideoPreview` from the tape: every animation tick, call
     `findFrameAt(tape, currentLmcTime)`, convert the returned frame's
     bytes to a `juce::Image` via swscale, hand to
     `VideoPreview::setFrame`.
  4. Validate `FrameMembrane` against real frame-rate-mismatched content:
     a 24 fps clip played at 30 fps display should stutter on the
     repeated frame; a 30 fps clip played at 24 fps display should skip
     the dropped frame. The math is already tested; this confirms the
     end-to-end pipeline preserves it.
  5. Run the multi-minute audio/video LMC-lock test the plan asks for —
     the membrane has the math; the runtime needs to honour it across a
     long playback.

- **Headless verification already done for M6:** the VideoFrame data
  model (metadata + opaque bytes), `Tape<VideoFrame>` (same shared
  template every Sirius tape uses), `findFrameAt` (the most-recent-frame-
  at-or-before-query rule, with the empty-tape, before-first, on-frame,
  between-frames, and past-end cases pinned down), and `FrameMembrane` /
  `convertFrameRate` (exact-Rational nearest-frame selection, including
  the awkward broadcast rates 23.976 / 29.97 / 59.94 staying exact, the
  24→30 stuffing pattern, the 30→24 dropping pattern, and the offset-
  start case) — 191 tests pass total. The `VideoPreview` JUCE component
  is a thin letterboxing renderer that takes a `juce::Image`; producing
  that image from a `VideoFrame` is the FFmpeg-bound work above.

### 2026-05-15 — Load dialog still cannot select `.sirius.json` on macOS

- **Files:** `app/MainComponent.cpp` (`chooseFileAndLoad`),
  `app/CMakeLists.txt` (the `PLIST_TO_MERGE` TCC keys).
- **What was deferred:** the macOS NSOpenPanel greys out
  `session.sirius.json` (and `.md` files) in `~/Downloads` no matter what
  is tried. Save side writes the file fine; only Load is broken.
- **What was already attempted (all failed):**
  1. Filter `*.sirius.json;*.json` (commit `cad8cb9`).
  2. Filter `*.json` (commit `f9deccc`).
  3. Filter empty `juce::String()` (commit `c041936`). JUCE source review
     showed this is *worse* — when `filters.size()==0`, the
     `shouldEnableURL` delegate in
     `juce_FileChooser_mac.mm:279-288` falls through to a
     directory-only allow, so every non-directory greys out.
  4. Filter `"*"` (uncommitted). JUCE source says this should pass both
     `createAllowedTypesArray` (returns `nil`, no UTI restriction) and
     the delegate's `matchesWildcard` check, allowing every file. In
     practice the file was still greyed.
  5. Non-native dialog via the `useOSNativeDialogBox=false` constructor
     parameter (uncommitted). It worked but the UI is unacceptable and
     additionally triggered the macOS TCC prompt for Downloads — which
     is the diagnostic clue the bug isn't in the filter at all.
  6. Added the four protected-folder TCC keys
     (`NSDownloadsFolderUsageDescription`,
     `NSDocumentsFolderUsageDescription`,
     `NSDesktopFolderUsageDescription`,
     `NSRemovableVolumesUsageDescription`) to the Info.plist via
     JUCE's `PLIST_TO_MERGE`. Verified the keys land in
     `Contents/Info.plist`. Still greyed.
- **Working hypothesis:** the ad-hoc-signed bundle
  (`codesign -dv` reports `flags=0x20002(adhoc,linker-signed)`,
  `Signature=adhoc`, no entitlements) is not trusted enough by macOS
  to receive the TCC permission prompt at all, so the keys we added
  are present but inert. `tccutil reset` against the bundle ID does
  nothing because no TCC record exists. The combination of "no
  Developer ID signature + protected folder + ad-hoc bundle" appears
  to be the failure mode.
- **What's needed to finish:**
  1. Sign the bundle with a Developer ID Application certificate (or
     enable the hardened runtime + entitlements at minimum) so macOS
     issues a TCC prompt. Quickest test: `codesign --sign - --deep
     --force --entitlements <plist> "Sirius Looper.app"` with a
     minimal entitlements file containing
     `com.apple.security.files.user-selected.read-write`.
  2. Or: investigate why `Elephant.png` was selectable while
     `.sirius.json` / `.md` were greyed in the same panel. That
     asymmetry suggests macOS may be applying a file-type-class
     allowlist (images pass, opaque-data files don't) when the bundle
     is below the trust threshold — independent of the JUCE filter.
  3. Or: ship the load via a drag-and-drop target on the Preparation
     pane (`FileDragAndDropTarget`) as a secondary path. The user
     drags `session.sirius.json` from Finder onto the app window;
     bypasses NSOpenPanel entirely. Save stays as-is.
- **Out of scope until then:** continuing with the rest of the headless
  coding work. Returning to this bug after that is finished.
- **State left in tree:** `app/CMakeLists.txt` has the TCC keys
  committed; `app/MainComponent.cpp::chooseFileAndLoad` is restored to
  the simple `"*.json"` filter (matches the f9deccc baseline before the
  empty-filter experiment).
