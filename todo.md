# Sirius Looper — Deferred Items

### 2026-05-15 — OTTO Look-and-Feel integration (cross-app)

- **Files:** new top-level shared module (location TBD — see open
  question below), `ui/` (new `sirius::LookAndFeel` subclass),
  `app/Main.cpp` (currently calls `juce::Desktop::getInstance()
  .getDefaultLookAndFeel()` at line ~38 — replace with the shared
  Sirius L&F), CMake wiring on both OTTO and Sirius sides.
- **What was deferred:** wholesale visual integration with OTTO's
  existing design system. Sirius today runs JUCE stock `LookAndFeel_V4`
  with default colours and fonts; the user wants Sirius to visually
  match OTTO so the two products read as one family.
- **What OTTO already has (inventoried 2026-05-15):**
  Located at `/Users/larryseyer/AudioDevelopment/OTTO`.
  - **`src/otto-plugin/ui/OTTOColours.h`** (~240 lines):
    - Layered dark-theme palette: `bg0`..`bg6` (darker = further back,
      lighter = elevated). `bg0`/`bg1` are `0xff010000` (matches OTTO
      logo background — operator-locked); `bg2` panels; `bg3` modals;
      `bg4`/`bg5`/`bg6` interactive states.
    - Named borders, text tiers (`textPrimary` / `Secondary` /
      `Disabled` / `Inverse`).
    - Accent: `accent = #00d4aa` (teal), `accentBright`, `accentDim`.
    - **8 player colours**: coral, rose, gold, mint, orange, lavender,
      leaf, sky. These map naturally to Sirius's per-tape identity —
      the kind-colour band on TimelineView strip heads becomes a
      player-colour band.
    - Semantic state colors (success/warning/error), meter colors,
      transport colors (`playActive = #4ade80`, `stopActive = #f87171`),
      mute/solo/fill action colors, slider track/thumb, focus outline,
      menu highlight.
  - **`src/otto-plugin/ui/OTTOLookAndFeel.h/cpp`** (2154 lines):
    - Subclass of `juce::LookAndFeel_V4`.
    - Custom draw for: buttons (gradient backgrounds), text buttons,
      toggle buttons, tick boxes, linear sliders, rotary knobs (270°
      arc), labels (typography hierarchy by ComponentID), combo boxes,
      popup menus (radial-gradient backgrounds, section headers, scroll
      arrows), transport buttons (play/pause/stop), mute/solo buttons
      with hue, pattern grid cells (OTTO-specific), focus outlines.
    - Touch-first sizing — 44pt minimums (Apple HIG), HiDPI snap-to-
      pixel helpers, 4px spacing grid, named border radii.
    - Audio-pro section title convention: ALL CAPS, BOLD, CENTERED —
      FabFilter/UAD/Waves visual language. Helpers
      `applySectionTitleStyle(juce::Label&, text)` and
      `drawSectionTitle(g, bounds, text)` are the single source.
    - Roboto Condensed-based menu design tokens (`getMenuItemFont()`
      etc.) — JUCE's PopupMenu and an OTTO-internal touch popup share
      one tokenset.
  - **`assets/Fonts/`**: 10 families bundled as TTF binary-data:
    Roboto, Roboto Condensed, Orbitron, Bricolage Grotesque (5
    weights), JetBrains Mono (3 weights), Montserrat, Open Sans,
    Phosphor (icons), Playfair Display.
- **Decision made this session:** **shared-submodule model.**
  User picked this from a 4-option list (shared submodule / vendor
  copy / extract subset / re-implement-native). Rationale: sister apps
  that always ship together but sold individually — drift between
  their visual identities is a permanent tax; one source of truth is
  worth the one-time extraction cost.
- **Open question (next session must decide first):** *where the
  shared module physically lives.* Four candidates were ready to
  present:
  1. **New top-level repo** (e.g.
     `~/AudioDevelopment/audio-ui-core/`). Cleanest separation; both
     OTTO and Sirius pull via git submodule or CMake FetchContent.
     One-time cost: extract code from OTTO into the new repo, update
     OTTO to consume from it, add the consumer to Sirius. **Working
     recommendation** — most-aligned with the "shared submodule"
     answer.
  2. **OTTO's `ui/` becomes the canonical home**, Sirius adds OTTO as
     a submodule. Faster bootstrap but couples Sirius's CMake to all
     of OTTO (HISE assets, sampler code) and OTTO becomes a hard
     dependency for any Sirius build.
  3. **Sirius hosts** the shared module, OTTO consumes. Unusual since
     OTTO has the source today.
  4. **Pragmatic two-stage**: vendor what Sirius needs from OTTO now
     (commit as `ui/sirius/lookandfeel/` inside Sirius), and schedule
     the extraction-into-shared-module as a follow-up cross-repo
     session. Keeps Sirius UI velocity high; pays the refactor cost
     once both apps are visually settled. **Pragmatic fallback** if
     full shared-module setup is too much for one session.
- **Why deferred:** wholesale L&F adoption + cross-repo submodule
  setup is its own session-sized piece of work. The current session
  is TimelineView operator verification + small polish items; mixing
  the two bloats context.
- **What's needed to finish:**
  1. **Pick a module home** from the four options above. Working
     recommendation: option 1 (new top-level repo). Pragmatic fallback:
     option 4 (vendor first, extract later).
  2. **If option 1:** create the new repo, extract `OTTOColours.h`,
     `OTTOLookAndFeel.h/cpp` minus OTTO-specific widgets (pattern
     cells, player badges), plus the font binary-data infrastructure,
     into it. Rename namespace from `otto::` to whatever the shared
     namespace is (`audiodev::ui::`? `larryseyer::ui::`? — open).
     Update OTTO's CMake to consume from the shared module. Add the
     shared module as a submodule to Sirius. Bind a `sirius::LookAndFeel`
     subclass that extends the shared base with any Sirius-specific
     widgets (timeline pills, tape strips). Install it in
     `app/Main.cpp`.
  3. **If option 4:** copy `OTTOColours.h` and a stripped
     `OTTOLookAndFeel.h/cpp` (no pattern cells, no player badges) into
     `ui/include/sirius/` and `ui/src/`, rename `otto::` to `sirius::`,
     copy the relevant fonts from `OTTO/assets/Fonts/` to
     `Resources/Fonts/`, wire them as JUCE binary-data, install the
     L&F in `app/Main.cpp`. File a follow-up todo for the shared-module
     extraction.
  4. **Apply the L&F to TimelineView.** The strip-head kind-colour
     band becomes one of OTTO's 8 player colours (per-tape identity).
     Pills use the OTTO accent (`#00d4aa`) for the loop-on indicator,
     the membership-outline uses `borderStrong`. Section title
     convention (ALL CAPS BOLD CENTERED) applies to tab labels.
  5. **Apply to the bottom bar.** Transport buttons (Arm / Mark In /
     Mark Out / Undo / Redo) use OTTO's `drawButtonBackground` +
     mute/solo/transport hue conventions — Arm shows red when armed
     (mute family), Mark In/Out are accent-coloured action buttons.
- **Open sub-decisions for the next session:**
  - **Font subset.** OTTO ships 10 families; Sirius likely needs only
    3–4 (Roboto for body, Orbitron for display, Roboto Condensed for
    menus, JetBrains Mono for time/numeric readouts on the diagnostics
    row). Pick which to bundle to keep the .app bundle small.
  - **Shared namespace name.** Going from `otto::Colours::` to
    `audiodev::ui::Colours::`? `larryseyer::ui::`? `aui::`? Naming
    affects every call site in OTTO when the rename lands.
  - **Player-colour → tape-id mapping.** OTTO has 8 player colours;
    Sirius's demo uses tape ids 100/200/300/400. Need a stable
    `getPlayerColour(tapeId)` mapping (modulo 8? a registered
    per-tape colour stored in `InputDescriptor`?). The latter is
    nicer for performer agency (operator picks the colour) but
    larger scope.
  - **Touch targets.** OTTO is touch-first (44pt min, iOS in scope).
    Sirius is touch-friendly-on-iPad but desktop-primary for now.
    Adopt OTTO's 44pt minimums anyway (no harm on desktop), or trim
    to denser desktop sizing for Sirius? Working recommendation: keep
    44pt — the eyes-free performance metaphor (white paper 14.4)
    benefits from big targets even on desktop.
- **Headless verification implications:** none of this changes the
  testable `*ViewState` selectors — they remain JUCE-free. The L&F
  affects only the renderer half (`TimelineView`, `PerformanceView`,
  `PreparationView`, `MainComponent`). All 226 existing tests should
  pass unchanged across the L&F swap.

### 2026-05-15 — M5: Plugin scanner crashes + scan-strategy redesign

- **Files:** `host/src/PluginScanner.cpp`, `host/include/sirius/PluginScanner.h`,
  eventually the app-level integration that triggers scans and the
  persistence layer that caches scan results.
- **Symptom (operator report, 2026-05-15):** the standalone app crashed
  during a plugin-folder scan. Scan was taking the "usual LONG time" —
  same UX problem Logic Pro exhibits when a system has thousands of
  plugins installed. No crash log captured yet.
- **What was deferred:** treating this as a session-of-its-own design
  problem rather than wedging it into the current TimelineView-polish
  session. Three intertwined pieces:
  1. **Crash protection.** A scan that ploughs through every VST3/AU on
     disk inevitably encounters at least one plugin whose constructor
     throws, deadlocks, or segfaults. JUCE's `AudioPluginFormatManager`
     instantiates plugins in-process; a bad plugin crashes the host.
     The standard mitigation is out-of-process scanning — a small
     child-process probe per plugin, with the parent timing it out and
     marking the plugin as failed if the child dies. JUCE's
     `KnownPluginList::scanAndAddDragAndDroppedFiles` and
     `AudioPluginFormat::createPluginInstanceFromDescription` both have
     async overloads; the child-process pattern is documented in
     JUCE's `PluginListComponent` source.
  2. **Light scan mode (file metadata only, no instantiation).** For
     users with thousands of plugins, deep-scanning every one on first
     launch is hostile. A "light" scan reads only what the OS can
     cheaply tell us — bundle name, format, manufacturer string from
     the Info.plist (AU) or `moduleinfo.json` (VST3 3.7+) — and defers
     full instantiation (which is what discovers parameter counts,
     factory presets, etc.) until the user actually drops the plugin
     into an effect chain.
  3. **Lazy-instantiate-on-first-use ("scan when loaded").** The
     operator's "outside the box" idea: skip the upfront full-scan
     entirely. Show every plugin the OS knows about (from the standard
     paths) immediately; pay the per-plugin instantiation cost the
     first time the user *uses* that plugin, and cache the result.
     This is what Bitwig does. Plays well with light scan (#2) — light
     scan populates the visible list, full scan happens on demand and
     persists.
- **Why deferred:** the current session is TimelineView operator
  verification + small UI polish. Plugin-scanner redesign is its own
  design problem (cache schema, child-process protocol, UI for
  failed-plugin reporting, persistence-format migration since
  `PluginScanner::Result` shape will change). Worth a dedicated session
  with a brainstorm pass and a written design before code.
- **What's needed to finish:**
  1. **Capture a crash report.** Next time the scanner crashes, grab
     the `~/Library/Logs/DiagnosticReports/Sirius Looper-*.crash` file
     and attach it to this entry — that determines whether the crash
     is in our scanner code, in JUCE's plugin loader, or in a specific
     plugin's constructor.
  2. **Design session (separate brainstorm):** decide between
     out-of-process child scanning vs. an in-process try/catch +
     watchdog timer, and decide whether to ship light-scan + lazy-load
     together or sequentially.
  3. **Implement crash protection first** — even an in-process
     try/catch around `createPluginInstance` plus a per-plugin timeout
     would have caught today's crash. Out-of-process can come later;
     the immediate value is "scan completes even when a plugin
     misbehaves."
  4. **Then implement light scan as the default**, with a "deep scan
     this one" gesture on the plugin row when the user wants the full
     parameter list before loading.
  5. **Lazy-instantiate-on-first-use** is the destination architecture;
     ship it once the cache + UI surfaces from steps 3-4 exist.
- **Headless verification already done for M5:** scanner
  format-registration, descriptor structures, GenericParameterView
  construction against a JUCE `AudioProcessor`. The instantiation /
  scan-runtime path is the operator-deferred half and is precisely
  where today's crash lives.

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

### 2026-05-15 — Session directory format (Whitepaper V2 §7.8)

- **Files:** `persistence/src/SessionFormat.cpp`,
  `persistence/include/sirius/SessionFormat.h`, `app/MainComponent.cpp`
  (Save/Load callers), eventually `persistence/src/TapeStore.cpp`.
- **What was deferred:** Whitepaper V2 §7.8 says a session is a
  directory `my-session.sirius/` containing:
  ```
  session.json            # the Constituent graph
  lmc-discipline.json     # LMC discipline history (Part 4.5)
  calibration/            # per-device latency + clock calibration
      <device-id>.json
  tapes/                  # the data layer — content-addressed
      <tape-id-hash>.{caf,flac,mkv}
  ```
  Current code writes a single `session.sirius.json` file containing
  only the Constituent graph. LMC discipline history and per-device
  calibration are not persisted; `TapeStore` exists but is not
  bundled into the same session unit. A session today is not the
  self-contained "valid archival unit" §7.8 describes.
- **Why deferred:** the plan's "Remaining open item" (line 273-277)
  was to *check* the V2 worked example against M1 structs — that
  check is done in commit (this commit). The actual file-format
  refactor is a separate scope decision: it touches Save/Load
  (which still has the unresolved macOS Load dialog TCC bug, see
  next entry), and would change every saved session's on-disk shape.
- **What's needed to finish:**
  1. Add a `Session` aggregate type to `persistence/` that bundles
     {root Constituent, optional LMC discipline history, optional
     calibration records, references to the TapeStore-resident
     audio data}. `SessionFormat::serialize/deserialize` operate on
     `Session`, not bare Constituent.
  2. Change Save to write a directory: create
     `<chosen-path>/session.json`, `<chosen-path>/calibration/`, and
     a `<chosen-path>/tapes/` symlink or copy from the live
     TapeStore. JUCE's `File::createDirectory()` plus the existing
     JSON serialization are sufficient.
  3. Change Load to read a directory: detect the `.sirius/`
     directory, parse the constituent JSON, materialize the
     calibration records, attach the bundled `tapes/` to the
     runtime TapeStore (likely via content-address verification:
     hashes inside must match filenames).
  4. Resolve the macOS Load dialog TCC bug first — switching Load
     to "pick a directory" instead of "pick a file" may change
     which TCC failure mode we hit, and we should avoid stacking
     unknowns.
- **Headless verification already done:** the Whitepaper V2
  Appendix C consistency check is complete — every field used in
  C.1 (twelve-bar blues) and C.2 (4-against-7 polymetric phrase)
  is representable in the M1 structs. The V2 surface vocabulary
  ("Section", "Phrase", "Loop", "Slice") maps onto Constituent +
  the optional fields (`phraseMetadata`, `tapeReference`,
  `repetitionRules.cardinality`); the engine has no separate Slice
  type because §7.5 explicitly defines a slice as "a loop with
  cardinality = Once," and `RepetitionRules::defaultOneShot()`
  encodes that. The `is_role_fillable = false` on a Section in V2
  C.1 is V2 being expository — Sections do not carry
  PhraseMetadata, so they cannot be marked fillable, and the
  default is "not fillable" for everything outside a phrase.

### 2026-05-15 — Mark Out should announce the new region visibly

- **Files:** `app/MainComponent.cpp` (`onMarkOut`, `refreshDiagnostics`),
  possibly a new transient banner widget or a Performance-tab overlay.
- **What was deferred:** when the performer clicks Mark Out and a
  CaptureRegion is created, the only on-screen feedback is the textual
  diagnostics line on the Preparation tab (`Regions: N (last: ...)`).
  That is too subtle for a performance gesture (white paper 14.5 —
  "shape, color, position, motion," not text). The performer should see
  an unambiguous confirmation — a "Loop 3 captured" banner, a flash on
  the playhead, the new region highlighted on a timeline strip, or all
  three.
- **Why deferred:** out of scope of the capture-state-machine commit
  (`17915c4` + `2499273`); user wants other coding to move first.
- **What's needed to finish:**
  1. A transient on-screen confirmation. Suggested: a 1.5-second auto-
     fading label at the top of whichever tab is active, plus an audible
     click (white paper 14.9 — "every confirmation they need arrives
     through hearing"). Audible click depends on M2 audio wiring.
  2. A persistent capture-history widget on the Preparation tab — a
     scrolling list of regions with in/out times, length, and a button
     to promote the region into a Loop Constituent (which would push an
     undoable edit onto the undo stack — white paper 14.7).
  3. Promotion is the actual completion of the capture flow: a captured
     region today lives only in `MainComponent::capturedRegions_` —
     ephemeral RAM, not part of the session. Until promotion exists,
     captures evaporate on app exit. Treat #3 as the priority of this
     deferral; #1 and #2 are subordinate UX.

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
