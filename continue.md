# Session Continuation — 2026-05-14

## Current State

Sirius Looper is a JUCE 8.x project built milestone by milestone against the
approved plan at `/Users/larryseyer/.claude/plans/we-have-written-a-declarative-pearl.md`.

**Status: every milestone except M6 (video) has its headless-verifiable half
shipped.** Each milestone's operator-deferred half — real audio devices, real
plugins on disk, real eyes-at-the-screen UI testing, real networking — is
catalogued in `todo.md` against the milestone whose test commitment requires it.

- M0 (skeleton + CI) — done; operator owes the FFmpeg spike + window-launch
  verification + remote-push CI run.
- M1 (conceptual-time core) — done.
- M2 (real-time foundation, membrane, ASRC) — headless half done; operator
  owes audio-device wiring + loopback calibration + the in→tape→loop test.
- M3 (Constituent hierarchy, repetition, arrangement, render pipeline,
  minimal functional UI) — done.
- M4 (persistence + capability tiers + overload protection) — done.
- M5 (plugin hosting, generic parameter view) — headless half done; operator
  owes scanning real plugins + the parameter-automation round-trip test.
- M6 (video) — **not started**; gated on the operator's FFmpeg spike.
- M7 (full UI — Performance/Preparation views, sacred undo, latency
  budget) — headless half done; operator owes wiring views into the app and
  driving them with real gestures.
- M8 (ensemble — LMC election, CRDT merge, transport messages) — headless
  half done; operator owes the real network transport and the
  two-node-partition-and-rejoin milestone test.

**178 tests pass. Zero compiler warnings introduced by our code.** The
remaining warnings (one `-Wimplicit-float-conversion` in AsrcTests.cpp:43;
two `-Wfloat-equal` in RationalTests.cpp:177 and the Catch2 decomposer it
expands through) predate every M4+ commit.

## What Was Done in the Most Recent Session

In addition to M3, M4, M5 (all three covered in the prior continue.md), this
session shipped M7 and M8.

**M7 (commit `1d66cb1`):**

- `ui/UndoStack` — multi-level undo/redo over `shared_ptr<const Constituent>`
  snapshots. Push truncates the redo branch; depth cap drops the oldest;
  every entry carries an optional label for white paper 14.7's "visible"
  requirement; undo/redo on an empty branch are silent no-ops because a
  performer's reflex hits them anyway.
- `ui/LatencyBudget` — rolling-window latency tracker against the <30 ms
  causal-coupling budget (14.8). Records measurements, reports mean/worst/
  fraction-within-budget; absence of samples is reported as "meets budget"
  rather than as silent failure.
- `ui/PerformanceViewState` + `ui/PerformanceView` — the eyes-free
  glanceable surface. The selector walks the Constituent tree like the
  RenderPipeline does and reports the deepest *named container* the
  playhead is inside as the foreground phrase, with a one-line cycle
  status ("3 of 8", "loop 5", "once") and an honest "silent" report.
- `ui/PreparationViewState` + `ui/PreparationView` — the dense readout.
  One row per Constituent, depth-first with rising indent, surfacing
  effect-chain presence, local meter, local tempo map, and role-fillable
  flags.

**M8 (commit `0ea111d`):**

- `net/LmcElection` — Marzullo interval intersection on top of tier
  dominance on top of anchor override. Throws on bad input (empty list,
  inverted interval, more than one anchor). Within the dominant tier the
  narrowest-interval node wins as master after falsetickers are removed.
- `net/SessionMerge` — CRDT union semantics. Tape hashes union
  (content-addressing makes collision impossible); Constituent versions
  union (immutability eliminates conflict); active version is
  last-writer-wins on a wall-clock timestamp. Merge is commutative,
  associative, and idempotent — each property tested.
- `net/Transport` — header-only data model for the three coordination
  message kinds (LMC time announcement, marker event, transport state
  change) as a `std::variant`. No wire, by design — that's the
  operator-deferred half.

## How To Test What's Shipped

Everything below builds and runs from a clean checkout. From the repo root:

```bash
bash/setup-deps.sh                                  # only if external/ is empty
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                           # expect 178/178 passing
ctest --test-dir build                              # ctest's view of the same
```

The standalone macOS app is at:

```
build/app/SiriusLooper_artefacts/Release/Sirius Looper.app
```

Right now it hosts the `SessionInspector` from M3 (a demo Constituent tree
driven by a scrub slider). M7's `PerformanceView` / `PreparationView` and
M5's `GenericParameterView` are built into `Sirius::Ui` / `Sirius::Host` but
not yet wired into the app's main window — that wiring is what the M7
operator deferral asks for. The full operator-verification matrix lives in
`todo.md`, milestone by milestone.

## Repo Layout

```
core/         JUCE-free conceptual-time engine (Rational/Meter/TempoMap/
              Position/TimeDomain/Constituent/RepetitionRules/Arrangement/
              EffectChain/PluginDescriptor/ParameterAutomation)
engine/       JUCE-free real-time layer (lock-free SPSC, retroactive ring,
              LMC, sample clock, calibration, membrane, LoopRenderer, ASRC,
              RenderPipeline, OverloadProtection)
host/         JUCE plugin-host runtime (PluginScanner, GenericParameterView)
persistence/  Session JSON format, content-addressed TapeStore
ui/           JUCE-free state selectors + thin JUCE views for Performance
              and Preparation, UndoStack, LatencyBudget
net/          LMC election, CRDT session merge, transport message types
              (JUCE-free, no real networking yet)
app/          Standalone JUCE app shell + capability tier + demo session
tests/        Catch2 — 178 test cases, 3843 assertions
external/     Vendored deps (JUCE, Catch2, soxr) — gitignored; setup-deps.sh
patches/      Local patches applied during setup-deps.sh
licenses/     AGPLv3 + App Store exception, plus drum-library + JUCE notices
```

## Key Decisions Made Across the Project

| Decision | Rationale |
|----------|-----------|
| AGPLv3 + App Store exception, OTTO licensing model | Matches sister app OTTO; user explicitly said. |
| `external/` vendoring, gitignored, `setup-deps.sh` | Matches OTTO. |
| Conceptual time = exact `Rational` (int64 num/den, overflow throws) | White paper's "exact by construction." |
| Engine core stays JUCE-free | Testability — Appendix D verification philosophy. |
| Five repetition dimensions as `std::variant`s | Illegal combinations unrepresentable. |
| Headless/operator split | Project convention — operator runs hardware/GUI tests. |
| ASRC uses soxr **variable-rate** path | Continuous drift-correcting membrane; ~2 ms latency. |
| Persistence depends on juce_core + juce_cryptography only | Lightweight, testable, gives JSON + File + SHA256. |
| `JUCE_ENABLE_MODULE_SOURCE_GROUPS=OFF` | When ON, plain `add_library` targets that link a JUCE module pull every module .cpp into their build set, but JUCE's `HEADER_FILE_ONLY` fixup runs only for `juce_add_*` targets. Off costs only IDE source-group display. |
| `CoreAudioKit` framework on macOS for SiriusHost | `juce_audio_processors.mm` uses `AUGenericView`. `juce_add_gui_app` adds the framework automatically; a plain library does not. |
| Parameter automation is `Tape<ParameterEvent>` | White paper Part 7.7 recursion — the same Tape template carries audio and automation; "automation curves are Constituents over parameter tapes." |
| UndoStack stores `shared_ptr<const Constituent>` only | Copy-on-write makes diffs unnecessary — the white paper's "trivial undo" claim made executable. |
| `MergeableSession` keeps every version, picks active by LWW | Matches white paper 12.6 literally: "Tapes union, Constituents union, active selection LWW." |
| Anchor override takes precedence over tier in election | White paper 12.4: musical authority outranks technical authority. |

## Operator Verification — What's Pending

`todo.md` has structured entries against each milestone. The short list:

- **M0** — install FFmpeg locally, write a small probe, confirm libav links on macOS/Windows/Linux. Launch `Sirius Looper.app` and confirm the window opens. Push to a GitHub remote and confirm the CI matrix goes green.
- **M2** — wire `AudioDeviceManager` to the membranes, run a one-time loopback calibration, do the in→tape→loop test.
- **M5** — install at least one VST3 and (on macOS) one AudioUnit; run `PluginScanner`; instantiate one of each; wire the parameter view to it; confirm a parameter movement records onto a `Tape<ParameterEvent>` and plays back.
- **M7** — wire `PerformanceView` and `PreparationView` into the app's main window; hook `UndoStack` into every edit path; feed UI frame latencies to `LatencyBudget`.
- **M8** — pick and implement a transport, hook each node's clock-discipline source into a `NodeClockEstimate`, run the two-node partition-and-rejoin milestone test.

The architecture all the way down is now in place; what remains is the operator-run cross-section that exercises it on real hardware, real plugins, real network, and a real performer's hands.

## Commands to Run First

```bash
cd "/Users/larryseyer/Sirius Looper"
bash/setup-deps.sh                                  # only if external/ is empty
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests                           # 178/178
```

## Open Questions

- **M6 video format strategy** — the plan flags a custom video tape format
  + an intra-frame codec choice. Best decided after the FFmpeg spike, which
  reveals what's cheap to read/write on every platform.
- **M8 transport choice** — the plan deliberately does not commit. OSC over
  UDP and Ableton Link's discovery layer are both plausible.
- **Role-fillable phrase resolution** — the data model is in place; the
  resolution logic (matching a `RoleSlot` against a pool of candidates at
  play time) is a novel, untested UX question the white paper itself flags.
