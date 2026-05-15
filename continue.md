# Session Continuation — 2026-05-15

## Top-of-page summary

Sirius Looper is a JUCE 8.x project built milestone by milestone against the
approved plan at
`/Users/larryseyer/.claude/plans/we-have-written-a-declarative-pearl.md`.

**Every milestone in the plan now has its headless-verifiable half shipped
(M0 → M8) and the standalone app exercises most of it through a four-tab
MainComponent.** 191 tests pass; the build is warning-free for any source we
control. The remaining work is all operator-side hardware/network/codec
validation, plus one known UX bug in the Save/Load dialog described below.

## ⚠ Open bug carried into the next session

**Load dialog still cannot select `.sirius.json` files on macOS** even after
the commit `f9deccc` filter fix. Two filter pattern have been tried:

- `*.sirius.json;*.json` — original (commit `cad8cb9`)
- `*.json` — current (commit `f9deccc`)

Neither lets the user pick the file. The save side works — files write to
disk just fine. So the bug is purely in JUCE's load-side filter behaviour.

**Things to try next session, in order of likelihood:**

1. Empty filter (`""`) — show every file regardless of extension. Confirms
   the file is selectable when no filter is applied. If yes, the filter
   string is the problem; if no, something else is going on (security,
   permissions, double-click vs single-click on macOS file picker).
2. `"*"` instead of `"*.json"` — different wildcard syntax may behave
   differently on macOS. JUCE's docs say semicolon-separated patterns, and
   the pattern is converted to UTIs on macOS.
3. Pass `nullptr` for the patterns argument — JUCE may treat that as "all
   files allowed."
4. Check the actual file on disk — `file /path/to/session.sirius.json` to
   confirm it really is what we think it is. Possibility: the saved file
   has no extension because `replaceWithText` ignored the suggested name's
   extension somehow.
5. Test with a sibling JSON file the user created with TextEdit — if that
   one is selectable but our saved one isn't, the problem is in the file
   itself (likely the macOS metadata "kind" attribute), not the filter.
6. The macOS Save panel may have appended a sandbox-derived UTI to the
   saved file. Check with `xattr -l` on the saved file.

**Where the code lives:** `app/MainComponent.cpp`,
`MainComponent::chooseFileAndLoad` (around the FileChooser construction).
Save is `MainComponent::chooseFileAndSave` and is fine.

## Current State

| Milestone | Status |
|---|---|
| M0 — skeleton + CI | done; operator owes FFmpeg spike + window-launch + remote-push CI |
| M1 — conceptual-time core | done |
| M2 — real-time foundation, membrane, ASRC | headless half done; operator owes device wiring, loopback calibration, in→tape→loop test |
| M3 — Constituent hierarchy + arrangement + render pipeline + minimal UI | done |
| M4 — persistence + capability tiers + overload protection | done |
| M5 — plugin hosting + parameter view | headless half done; operator owes real-plugin scan + automation round-trip |
| M6 — video | data model + membrane done; operator owes the full FFmpeg pipeline |
| M7 — full UI (Performance/Preparation/UndoStack/LatencyBudget) | done; operator owes gesture-loop wiring and real latency measurement |
| M8 — ensemble (LMC election, CRDT merge, transport messages) | data model done; operator owes the real network transport and two-node test |
| App wiring | done; **Load dialog filter bug above** |

**191 tests pass. Zero compiler warnings from any source we control.** The
three pre-existing float warnings in AsrcTests:43 and RationalTests:177 were
cleaned in commit `cad8cb9`.

## What's Inside Each Library

```
core/         JUCE-free conceptual-time engine. Types: Rational, Meter,
              TempoMap, Position, TimeDomain, Constituent, ConstituentId,
              RepetitionRules (Trigger/Cardinality/Phase/Mutation/Termination
              as std::variants), Phrase, Tape<T>, TapeId, TapeReference,
              Arrangement (sequence/layer + RoleSlot), EffectChain,
              EffectChainEntry, PluginDescriptor, ParameterEvent
              (parameter automation tape payload).
engine/       JUCE-free real-time layer. Types: LockFreeSpscQueue,
              RetroactiveRing, Lmc, MonotonicClock, SampleClock,
              AudioDeviceCalibration, Membrane (latency compensation),
              LoopRenderer, Asrc (libsoxr wrapper), RenderPipeline,
              OverloadProtection (priority-ladder shedding).
persistence/  Session round-trip. Types: SessionFormat (Constituent-graph
              JSON serialize/deserialize), TapeStore (content-addressed
              blob store, SHA-256 filenames). Depends on Sirius::Core +
              juce_core + juce_cryptography.
host/         Plugin host runtime. Types: PluginScanner (VST3/AU/AUv3),
              GenericParameterView (JUCE Component, one row per parameter,
              two-way binding via AudioProcessorParameter::Listener).
              Depends on Sirius::Core + juce_audio_processors +
              juce_gui_basics + CoreAudioKit (macOS).
ui/           Performer's instrument. Types: UndoStack (multi-level over
              shared_ptr<const Constituent>), LatencyBudget (rolling
              window vs <30 ms target), PerformanceViewState selector +
              PerformanceView component, PreparationViewState selector +
              PreparationView component.
net/          Ensemble. Types: DisciplineTier, NodeClockEstimate,
              ElectionResult, electLmc (Marzullo + tier dominance +
              anchor override), MergeableSession, merge() (CRDT union),
              activeVersions (LWW), EnsembleMessage (variant of
              LmcTimeAnnouncement / MarkerEvent / TransportStateChange).
video/        Video subsystem (headless half). Types: VideoFrameMetadata,
              VideoFrame, VideoPixelFormat, VideoTape (alias for
              Tape<VideoFrame>), findFrameAt, FrameMembrane (nearest-frame
              rate conversion), convertFrameRate, VideoPreview component.
app/          Standalone shell. Types: CapabilityTier + selectTier +
              policyFor + TierPolicy (M4 startup assessment, JUCE-free),
              DemoSession (builds the in-process demo Constituent tree),
              MainComponent (the TabbedComponent host with Performance/
              Preparation/Plugins/Video tabs and the bottom playhead +
              undo/redo bar).
tests/        Catch2 — 191 test cases, 3896 assertions.
external/     Vendored deps (JUCE, Catch2, soxr) — gitignored. Run
              bash/setup-deps.sh on a fresh checkout.
patches/      patches/soxr-quote-paths.patch — fixes soxr's CMake when the
              source path contains a space (the literal "Sirius Looper").
licenses/     AGPLv3 + Apple App Store exception. Sample library separately
              licensed.
```

## Operator-Verification Matrix (full detail in `todo.md`)

Each milestone with operator-deferred work has a `### YYYY-MM-DD` block in
`todo.md` enumerating files, what was deferred, why, what's needed to finish,
and what headless verification has already been done. Short version:

- **M0** — install FFmpeg + decode-one-frame probe on macOS/Win/Linux,
  launch the app bundle and confirm the window opens, push to a GitHub
  remote and confirm the CI matrix.
- **M2** — wire `AudioDeviceManager` to the membrane code, run a one-time
  loopback latency calibration, demonstrate the in→tape→mark→loop cycle.
- **M5** — install at least one VST3 (and on macOS one AU), scan with
  `PluginScanner`, instantiate via `AudioPluginFormatManager::createPluginInstance`,
  bind a `GenericParameterView`, capture parameter movements onto a
  `Tape<ParameterEvent>`, replay them.
- **M6** — once the M0 FFmpeg spike is done: wire the decode pipeline that
  fills `VideoFrame::pixels`, the encode pipeline that writes intra-frame
  video tapes, the swscale conversion to `juce::Image` for `VideoPreview`,
  the multi-minute audio/video LMC-lock test.
- **M7** — wire `PerformanceView` and `PreparationView` into a real
  gesture loop with actual edits (the demo just exercises the undo of a
  rename), feed real frame-to-screen latency into `LatencyBudget` rather
  than the current Timer-jitter proxy.
- **M8** — pick a transport (OSC over UDP, Ableton Link's discovery, or
  something custom), implement it against `EnsembleMessage`, wire per-node
  clock-discipline sources to produce `NodeClockEstimate`s, run the
  two-node partition-and-rejoin milestone test using `merge()` on rejoin.

## The Standalone App Today

```bash
cd "/Users/larryseyer/Sirius Looper"
bash/setup-deps.sh                                  # only if external/ is empty
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                           # expect 191/191
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

The window has four tabs and a bottom control bar:

- **Performance** — `PerformanceView` centered. As you drag the playhead, the
  foreground phrase name and cycle status update. White paper 14.5 glanceable.
- **Preparation** — Save... / Load... / Reload-demo buttons + status label
  at the top; `PreparationView` (the Constituent tree with kind/duration/
  effect-chain/meter/tempo-map flags) in the middle; three-line diagnostics
  at the bottom (tier + policy, UI tick jitter against the 30 ms budget,
  undo state).
- **Plugins** — registered formats, "Scan a plugin folder..." button
  (`juce::FileChooser`, async), descriptor list. On macOS, default-location
  scanning was *not* wired because `AudioUnitPluginFormat` ignores the
  search path and enumerates every installed AU — that would hang the UI
  while it sweeps the whole machine. The folder-scan button avoids it.
- **Video** — `VideoPreview` with a status line about the pending FFmpeg
  pipeline. Empty until M6's runtime half lands.

Bottom bar across every tab: **Undo** / **Redo** buttons, playhead slider
in 1/16-second ticks (the engine still sees only exact rationals; no double
crosses into core/), and a numeric time readout.

## Save / Load — what works, what doesn't

- **Save** works. Click Save..., pick a name and folder, the file lands on
  disk with a JSON serialization of the current undo-stack top.
- **Load** is the bug above: the file picker shows files but won't let you
  *select* the `.sirius.json` one. Status label was supposed to read
  *"Loaded session.sirius.json"* on success or *"Load failed: <message>"*
  on a malformed file (rule 3 — degradation announced, not silent).
- **Reload demo** works — it pushes a fresh `buildDemoSession()` as an
  undo entry, so Undo brings you back to whatever was on screen before.

## Key Decisions Made Across the Project

| Decision | Rationale |
|----------|-----------|
| AGPLv3 + App Store exception, OTTO licensing model | User explicitly asked. |
| `external/` vendoring, gitignored, `setup-deps.sh` | Matches OTTO. |
| Conceptual time = exact `Rational` (int64 num/den, overflow throws) | White paper's "exact by construction." |
| Engine core stays JUCE-free | Appendix D verification philosophy. |
| Five repetition dimensions as `std::variant`s | Illegal combinations unrepresentable. |
| Headless/operator split | Project convention. |
| ASRC uses soxr **variable-rate** path | Continuous drift-correcting membrane; ~2 ms latency. |
| Persistence depends on juce_core + juce_cryptography | Lightweight, testable, gives JSON + File + SHA256. |
| `JUCE_ENABLE_MODULE_SOURCE_GROUPS=OFF` | Plain `add_library` targets that link a JUCE module otherwise pull every module .cpp into their build set. |
| `CoreAudioKit` framework on macOS for SiriusHost | `juce_audio_processors.mm` uses `AUGenericView`; `juce_add_gui_app` adds it automatically but a plain library does not. |
| Parameter automation is `Tape<ParameterEvent>` | White paper 7.7 recursion. |
| UndoStack stores `shared_ptr<const Constituent>` | Copy-on-write makes diffs unnecessary. |
| `MergeableSession` keeps every version, picks active by LWW | Literal mapping of white paper 12.6. |
| Anchor override takes precedence over tier in election | White paper 12.4: musical authority outranks technical. |
| Playhead slider ticks in 1/16 second | Slider value → exact Rational without a double ever entering the engine. |
| Load is an edit (pushes a new undo entry) | White paper 14.7: undo is sacred; load must not wipe history. |
| AU default-location scan deliberately not wired in the Plugins tab | Would hang the UI sweeping every installed AU. |

## Commands to Run First

```bash
cd "/Users/larryseyer/Sirius Looper"
bash/setup-deps.sh                                  # only if external/ is empty
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                           # 191/191
open "build/app/SiriusLooper_artefacts/Release/Sirius Looper.app"
```

## Suggested First Move Next Session

1. **Fix the Load dialog filter bug.** The diagnostic ladder is at the top
   of this file. Start with the empty filter `""` to confirm the file is
   *selectable* when not filtered out; then re-introduce the right pattern.
   Suspect ranked highest: macOS treats `*.json` as a UTI lookup that
   doesn't match files whose system "kind" was set by `replaceWithText`
   rather than by a proper Save panel.

2. After that, the project has no remaining headless work the plan asked
   for. The next thing of architectural value is whatever the operator
   gets to next on the testing matrix — usually that means the FFmpeg
   spike to unblock M6 video, or wiring real audio devices to unblock the
   M2 in→tape→loop demonstration.

## Open Questions

- **M6 video format strategy** — the plan flags a custom video tape format
  + an intra-frame codec choice. Best decided after the FFmpeg spike.
- **M8 transport choice** — the plan deliberately does not commit. OSC over
  UDP and Ableton Link's discovery layer are both plausible.
- **Role-fillable phrase resolution** — the data model is in place; the
  resolution logic (matching a `RoleSlot` against a pool of candidates at
  play time) is a novel, untested UX question the white paper itself
  flags.
- **Capture state machine** — the Performance view says "what is playing"
  but the white paper 14.5 also calls for "what is captured, what is
  about to happen." Those need a capture state machine that does not yet
  exist; deliberately deferred because the audio device wiring (M2
  operator-side) drives it.
