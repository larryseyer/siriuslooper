# CLAUDE.md — Sirius Looper (project instructions)

Project-specific rules for Sirius Looper. The user-level `~/.claude/CLAUDE.md`
(commit discipline, hard-stops, code standards, iOS/JUCE platform rules) applies
everywhere and is NOT repeated here. **Always read `continue.md` first** — it is
the live session handoff (what just shipped, what's queued).

---

## Current focus (2026-05-20)

Building the **white-paper mixer + GUI architecture** (Part VI: a full creative
mixer on each side of the tape) now — **resequenced earlier** than V7's milestone
order had it (the engine-first order parked operator mixer UX at "M22+"), because
the app must be operator-testable. This is white-paper work pulled forward, **not
a change of direction**. The OTTO visual parity (L&F, mixers, transport, pills,
built-in FX) is the established sister-app product intent. Engine milestones
(M8 S7+) resume after. See `docs/design/mixer-design.md` + `continue.md`.

## Sister app: OTTO

- OTTO lives at `/Users/larryseyer/AudioDevelopment/OTTO` and is a **READ-ONLY
  reference**. Copy/learn FROM it; **never edit it.**
- Sirius and OTTO ship together but are sold separately and must each build
  independently. Shared look-and-feel is **vendored** (copied) into Sirius
  (`ui/lookandfeel/`), not aliased — a shared submodule is the eventual form.

## Architecture (non-negotiable)

- Canonical design doc: **`docs/Sirius Looper Whitepaper V7.md`** (the "why").
  `docs/design/` + `docs/superpowers/specs/` hold feature designs.
- Signal path: **input mixer → tape → output mixer**, with a direct layer for
  sub-ms monitoring. The tape is the always-running source of truth.
- **Input mixer = capture console** (physical/file inputs → process → out to
  tapes; NEVER takes a tape as input). **Output mixer = mixdown console** (one
  channel per phrase → process → stereo outputs/buses/master).
- Conceptual time is **exact `Rational`**, never floating-point, until the
  membrane renders. The LMC (Logical Master Clock) is the only honest timebase.
- Constituent hierarchy: tape → loop → phrase → section → song → set.
- Library layers (each its own CMake target): `core` (JUCE-free pure C++),
  `engine` (RT engine; juce_core is a PRIVATE .cpp detail, public headers
  JUCE-free), `audio`, `host` (out-of-process plugin hosting), `persistence`,
  `ui` (`SiriusUi` + vendored `SiriusLookAndFeel`), `app` (the `SiriusLooper`
  GUI app). `tests` builds `SiriusTests` (Catch2).

## ⛔ HARD INVARIANT: stereo only

All **audio** channels, buses, sends, outputs, and masters are **stereo pairs**.
There is **no mono audio anywhere** (matches OTTO). Mono sources are dual-mono
from the channel boundary inward. Engine routing, mixer UI, and metering all
assume stereo. (MIDI/video carry their own native payloads.) See whitepaper §6.1.

## ⛔ Audio-thread rules

The full contract is `docs/RT_SAFETY_CONTRACT.md`. On any function reachable from
the audio callback: `noexcept`, **no allocation, no locks, no I/O, no throw**.
Lock-free SPSC queues hand work to worker threads (see `TapeWriter`,
`WetCaptureWriter`, `NotificationBus`). Verify before touching `AudioCallback`,
`Bus`, `InputMixer`, `OutputMixer`, `Lmc`, or anything they call on the hot path.

## Build & run

```bash
# Configure + build (Ninja is the dev-loop generator; same path every build)
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target SiriusTests      # unit tests
cmake --build build --target SiriusLooper      # the macOS standalone app
ctest --test-dir build                         # full suite (baseline 449/450; the
                                               # 1 non-pass is the separately-run
                                               # MainComponentPluginEditorTests exe)
bash bash/test-s7.sh                           # plugin-editor lifecycle (operator)
```

- **Canonical app (only copy on disk):**
  `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app`. A Desktop alias
  **`Sirius Looper`** points at it — the operator launches via that alias. Do NOT
  create other build copies (a stale Finder alias to a deleted build once caused
  "no visible changes"). `build-xcode/` is intentionally gone.
- **Clean rebuild (`rm -rf build`) before asking the operator to eyes-on a GUI
  change.** CMake caches stale configs.
- GUI changes are **operator-verified, not unit-tested** (same status as other
  MainComponent wiring). The agent cannot keep the GUI alive from CLI — visual
  confirmation is the operator's. Engine work IS headless-testable — use TDD.
- macOS first; iOS hosts AUv3 only and is **Release-only** (Debug ruins audio).
  Platform order: macOS → iOS → Windows → Linux.

## Conventions specific to this repo

- Vendored OTTO code (`ui/lookandfeel/`) is kept **byte-faithful** to OTTO
  (identical names, `OTTOBinaryData` namespace) so re-syncing is a file copy;
  it compiles without Sirius's strict `-Werror` flags for that reason.
- Colour method (tapes/phrases/loops/pills) is documented in
  `docs/design/sirius-colour-method.md` — one source of truth in
  `ui/include/sirius/SiriusPalette.h`.
- Deferrals live in `todo.md`; the session handoff lives in `continue.md`
  (refresh it every session).
