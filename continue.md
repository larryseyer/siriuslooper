# Session Continuation — 2026-05-14

## Current State

Sirius Looper is a greenfield JUCE 8.x project being built milestone by milestone
against the approved plan at
`/Users/larryseyer/.claude/plans/we-have-written-a-declarative-pearl.md`.
Done so far: Phase 0 (white paper V2), M0 (skeleton + CI), licensing, M1
(conceptual-time core), M2 (real-time foundation + membrane math + ASRC — the
headless-verifiable half), and most of M3 (repetition/phrase data model + render
pipeline). **97 tests pass.** The next task is the **M3 arrangement primitives**.

## What Was Done This Session

- **Assessed white paper V1**, produced **`Sirius Looper Whitepaper V2.md`** — 8
  additive improvements (worked examples, block diagrams, plugin-host section,
  render-pipeline perf note, storage sizing, file-format sketch, testing
  philosophy appendix, trimmed decision log) plus **Appendix E** (a Sirius↔Reaper
  terminology map, added at user request).
- **M0**: CMake (≥3.22) + JUCE 8.0.12 + Catch2 3.15.0, standalone app shell,
  GitHub Actions CI matrix (macOS/Windows/Linux).
- **Licensing**: studied sister app OTTO at `/Users/larryseyer/AudioDevelopment/OTTO`;
  Sirius uses the same model — AGPLv3 + Apple App Store exception, proprietary
  Larry Seyer Acoustic Drum Library (separately licensed), JUCE/Ableton Link
  commercial licenses **already held**. Files: `LICENSE`, `LICENSE-THIRD-PARTY.md`,
  `SAMPLE-LICENSE.md`, `licenses/AGPL-3.0.txt`.
- **Vendoring conversion**: switched from FetchContent to OTTO-style `external/`
  vendoring — deps cloned as plain snapshots (gitignored, **not committed**),
  built via `cmake/Dependencies.cmake`, repopulated by `bash/setup-deps.sh`.
- **M1** — conceptual-time core (JUCE-free `SiriusCore`): `Rational`, `Meter`,
  `TempoMap`, `Position`, `TimeDomain`, `Constituent`, `Tape`/`TapeEvent`.
- **M2** — real-time engine (JUCE-free `SiriusEngine`): `LockFreeSpscQueue`,
  `RetroactiveRing`, `Lmc`/`MonotonicClock`, `SampleClock`,
  `AudioDeviceCalibration`, `Membrane` (latency compensation), `LoopRenderer`,
  `Asrc` (libsoxr variable-rate). Measured soxr VR/HQ latency: ~2 ms — well
  inside the <30 ms budget, so the custom-resampler fallback is not needed.
- **M3 so far**: `RepetitionRules` (five dimensions as `std::variant`s),
  `Phrase`/`PhraseMetadata`, `ConstituentId` + `TapeId` extracted to own headers,
  `Constituent` extended (`repetitionRules`, `phraseMetadata`, `tapeReference`),
  `RenderPipeline` (hierarchy rendering with polymetric domain composition).

## Key Decisions Made

| Decision | Rationale |
|----------|-----------|
| AGPLv3 + App Store exception, OTTO licensing model | User: Sirius ships an embedded smaller OTTO (sfizz sampler + drum-library subset); same licenses, incl. samples |
| `external/` vendoring, gitignored, `setup-deps.sh` | Match sister app OTTO; user explicitly asked |
| Conceptual time = exact `Rational` (int64 num/den, overflow throws) | White paper's "exact by construction" — no floating point in the engine |
| Engine core stays JUCE-free | Testability; the white paper's Appendix D verification philosophy |
| Five repetition dimensions as `std::variant`s | Illegal combinations unrepresentable |
| M2/M3 split: headless-verifiable vs operator-verified | Hardware/GUI testing is operator-run per CLAUDE.md; deferred items in `todo.md` |
| ASRC uses soxr **variable-rate** path, not constant-rate | A continuous drift-correcting membrane needs VR; it's also the path the plan flagged for latency measurement |
| `RepetitionRules`/`effect_chain` deferred out of M1's Constituent | They are M3/M5 subsystems; adding stubs in M1 would violate "no stubs" |

## Bugs Found / Fixed

- **soxr CMake breaks on the space in "Sirius Looper"** — unquoted `${PROJECT_SOURCE_DIR}`
  and misuse of `CMAKE_SOURCE_DIR` under `add_subdirectory`. Fixed via
  `patches/soxr-quote-paths.patch`, applied automatically by `setup-deps.sh`.
- **soxr CMP0115 dev-warnings** — suppressed cleanly with a scoped
  `CMAKE_POLICY_DEFAULT_CMP0115 OLD` in `Dependencies.cmake`.
- **`Rational` overflow test was wrong** (not the code) — `-(INT64_MIN+1)` does
  not overflow; corrected the test to the real boundary (`INT64_MIN` rejected at
  construction).
- **ASRC first measured 0 ms latency** — was using soxr's constant-rate path and
  querying `soxr_delay()` before processing. Switched to the variable-rate path;
  latency now measures correctly (~2 ms).
- **Most-vexing-parse** in `RenderPipelineTests.cpp` `makeLoop` helper — fixed
  with brace initialization.
- **Duplicate-library link warning** — `SiriusCore` listed both directly and
  transitively; dropped the redundant explicit link in `tests/CMakeLists.txt`.

## Files Modified

Repo layout now: `core/` (SiriusCore, JUCE-free), `engine/` (SiriusEngine,
JUCE-free), `app/` (standalone shell), `tests/`, `cmake/`, `bash/`, `patches/`,
`.github/workflows/`. White papers V1/V2 + all four license files at root.
See `git log --oneline` — 11 commits this session, from `927f534` (white papers)
through `e7909b6` (M3 render pipeline). `todo.md` tracks operator-verified
deferrals (M0 FFmpeg spike / window launch / CI; M2 device wiring / loopback
calibration / end-to-end audio test).

## Next Steps (Priority Order)

1. **M3 arrangement primitives** — `core/Arrangement.*`. White paper Part 11.3.
   Most "primitives" are just Constituent-tree operations; the design sketch:
   - `arrangement::sequence(parent, children)` — place children end-to-end
     (each child's `conceptualIn` = previous child's `conceptualOut`).
   - `arrangement::layer(parent, children)` — place children simultaneously
     (all sharing the parent's span / starting at the same `conceptualIn`).
   - `RoleSlot` — a role-fillable position (white paper 8.4 structured
     improvisation): a placeholder resolved to a Constituent by `role` at play
     time. This is the one genuinely-new type; keep it minimal for M3.
   - Add golden tests; keep `core/` JUCE-free.
2. **M3 minimal functional UI** — operator-verified (GUI testing is operator-run);
   build the app shell out enough to exercise the Constituent tree + render
   pipeline, then hand off launch verification to the operator via `todo.md`.
3. **M4** — persistence & capability tiers (`persistence/`, `app/CapabilityTier`).
   Then M5 (plugin hosting), M6 (video — needs the FFmpeg spike first), M7 (full
   UI), M8 (ensemble).

## Context the Next Session Needs

- **IDE diagnostics are stale after every build** — clangd lags behind the
  regenerated `compile_commands.json`. Trust `cmake --build` + `ctest`, not the
  editor squiggles.
- **`external/` is not committed** — a fresh clone must run `bash/setup-deps.sh`
  first (clones JUCE/Catch2/soxr as snapshots, applies the soxr patch).
- **The render pipeline's honest M3 limits** (documented in `RenderPipeline.h`):
  only `FreeRunning` triggers emit reads — other triggers are *correctly dormant*
  (no trigger subsystem yet, so they genuinely shouldn't sound). Cross-referential
  phase, the mutation engine, termination-as-stop-event, and stateful triggers
  are deliberately later subsystems — not stubs.
- **Per-milestone workflow this session**: build a coherent chunk → all tests
  green → `grep` changed files for TODO/FIXME/stub → single-line commit → report.
  Each milestone task tracked via the Task tools.
- Build is clean — zero compiler warnings is the bar.
- Working on `main`, no feature branches (per user CLAUDE.md). Nothing pushed
  (no remote configured — that's an operator decision).

## Commands to Run First

```bash
cd "/Users/larryseyer/Sirius Looper"
bash/setup-deps.sh                                  # only if external/ is empty
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build                              # expect 97/97 passing
```

## Open Questions

- **Role-based slots scope**: how much machinery for `RoleSlot` in M3? The white
  paper itself flags role-fillable phrases / structured-improvisation UX as a
  novel, untested open question — so keep the M3 data model minimal and don't
  over-build the resolution logic.
- **Operator verification cadence**: M0 and M2 each left hardware/GUI items in
  `todo.md`. At some point the operator should run them (launch the app window,
  do the FFmpeg spike, wire + test real audio devices) — worth surfacing before
  M6 (video) which depends on the FFmpeg spike.
