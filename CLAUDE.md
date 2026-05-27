# CLAUDE.md — IDA (project instructions)

Project-specific rules for IDA. The user-level `~/.claude/CLAUDE.md`
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

- OTTO is consumed as a **git submodule** at `external/OTTO/`. Single source
  of truth: bug fix in OTTO → `git submodule update --remote external/OTTO`
  → SHA bump in IDA → done. (The older byte-faithful vendored copy under
  `ui/lookandfeel/` was deleted in T0b on 2026-05-22.)
- IDA and OTTO ship together but are sold separately and must each build
  independently. IDA's installer bundles a FULL copy of OTTO; OTTO's
  paywalled features are runtime-gated. Licensing is **identical** between
  the two products.
- OTTO's assets (IRs, samples, fonts, GUI, models — ~3.7 GB total) are
  gitignored (copyright). Dev time: IDA's build references the operator's
  OTTO checkout at `/Users/larryseyer/AudioDevelopment/OTTO/assets/` via the
  `OTTO_ASSETS_DIR` CMake variable. Customer install time: the bundling
  pipeline copies OTTO's assets into the install bundle once; bundled-OTTO
  and IDA both read from the same path at runtime.

## Cross-Project Inbox Protocol (IDA ⇄ OTTO)

IDA's Claude has **full edit autonomy** on OTTO source. IDA and OTTO
communicate **AI-to-AI** via `external/OTTO/CROSS_PROJECT_INBOX.md`. The
operator is **NOT** a required reviewer of the back-and-forth.

### At session start (mandatory)

Read `external/OTTO/CROSS_PROJECT_INBOX.md`. For each unacknowledged entry
addressed to you (look under `[FROM OTTO → IDA]`):

1. Change `Status:` to `acked YYYY-MM-DD`.
2. Add a `Resolution:` line describing what action you took (bumped
   submodule SHA, refactored callers, etc.).
3. Act on the entry's guidance.

### When you edit OTTO source

IDA's session has full edit autonomy on OTTO, with mandatory awareness
propagation:

1. Make the OTTO change with a focused commit in `external/OTTO/`. Trailer:
   `Ida-Origin: <ida-sha>` (or `bootstrap` for the first protocol
   commit). For protocol commits where the SHA chicken-and-eggs, reference
   the most-recently-landed IDA commit.
2. Append a new entry under `[FROM IDA → OTTO]` in
   `CROSS_PROJECT_INBOX.md` describing the change, files touched, why,
   and what OTTO's Claude must do/avoid.
3. Push OTTO (`origin/main`).
4. Back in IDA: bump the submodule SHA, commit, push IDA.

### When OTTO needs IDA to act

OTTO's Claude can append entries under `[FROM OTTO → IDA]`. You'll see
them at session start and act (bump submodule, adapt callers, etc.) per
the entry's `For IDA's Claude:` line. Acknowledge by updating the entry.

### Entry format

```
## YYYY-MM-DD — <one-line subject>
Direction: IDA → OTTO        (or OTTO → IDA)
IDA commit: <sha>            OTTO commit: <sha>
Files touched: <paths>
Why: <one-paragraph rationale>
For <recipient>'s Claude: <specific guidance — do not revert, here's the migration, etc.>
Status: needs-ack | acked YYYY-MM-DD | resolved
Resolution: <added by recipient when ack'd>
```

### Pruning

The inbox is the active queue, not a historical log. At session start, after you have read the inbox and acted on every entry addressed to you, **delete every entry whose `Status:` is `acked YYYY-MM-DD` (or `informational`) and that carries a `Resolution:` line.** Never prune a `needs-ack` entry. Durable cross-project history lives in `git log --grep='Ida-Origin\|OTTO-Origin'` and in the original commit messages — the inbox does not need to duplicate it.

### Audit trail

`git log --grep='Sirius-Origin\|Ida-Origin' --all` in `external/OTTO/` surfaces
every IDA-originated OTTO commit forever, even after inbox entries are pruned.
(Pre-rename commits use the legacy `Sirius-Origin:` trailer.)

## Architecture (non-negotiable)

- Canonical design doc: **`docs/IDA_Whitepaper_V10.md`** (the "why").
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
  `ui` (`IdaUi` + vendored `IdaLookAndFeel`), `app` (the `IDA`
  GUI app). `tests` builds `IdaTests` (Catch2).

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
cmake --build build --target IdaTests      # unit tests
cmake --build build --target IDA      # the macOS standalone app
ctest --test-dir build                         # full suite (baseline 449/450; the
                                               # 1 non-pass is the separately-run
                                               # MainComponentPluginEditorTests exe)
bash bash/test-s7.sh                           # plugin-editor lifecycle (operator)
```

- **Canonical app (only copy on disk):**
  `build/app/IDA_artefacts/Release/IDA.app`. A Desktop alias
  **`IDA`** points at it — the operator launches via that alias. Do NOT
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
  it compiles without IDA's strict `-Werror` flags for that reason.
- Colour method (tapes/phrases/loops/pills) is documented in
  `docs/design/ida-colour-method.md` — one source of truth in
  `ui/include/ida/IdaPalette.h`.
- Deferrals live in `todo.md`; the session handoff lives in `continue.md`
  (refresh it every session).
