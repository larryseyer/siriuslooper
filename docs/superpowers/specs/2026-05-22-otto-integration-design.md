# OTTO Integration — Design Spec

Status: design approved (brainstorm 2026-05-22). Captures the four OTTO
integration decisions reached during the post-T0 brainstorming session
that paused the P7 umbrella plan's T1 (docs). This spec is the "why +
what"; per-decision implementation lands across the plan's remaining
slices T1–T6 plus the bug-fix slice that landed first
(commit `75c6866`).

---

## Context

Last session shipped T0a + T0b of the P7 umbrella plan — OTTO is now
consumed as a git submodule at `external/OTTO/`, replacing the prior
byte-faithful `ui/lookandfeel/` copy. With OTTO as a single source of
truth in the build, four product-level integration questions surfaced
that the brainstorm didn't anticipate at T0 planning time. Each had
real architectural implications worth pinning before resuming T1:

1. **OutputRouter** — does Sirius adopt OTTO's app-wide output-routing
   strategy, or supersede it?
2. **Edit-policy / governance** — when (and how) may a Sirius session
   edit OTTO source without operator friction?
3. **Internal-FX adapter shape (T3)** — how do Sirius's internal EQ /
   CMP / RVB / DLY adapters wrap OTTO's header-only Player FX?
4. **Assets** — where do IRs (and other OTTO assets) come from for
   Sirius, given OTTO's gitignored `/assets`?

A fifth concern (a meter bug the operator reported at the end of the
prior session) was also resolved during the brainstorm and shipped as
the first commit of this session's execution (`75c6866`).

---

## Decision 1: OTTO is a 32-stereo-input source to Sirius's Output Mixer

**Bundled OTTO presents 32 stereo outputs** (matches OTTO's PerDrum
fanout: 24 instruments + 4 FX returns + 4 player buses = 32 stereo
pairs). Each of those 32 outputs becomes an **additional channel strip
in Sirius's Output Mixer**, placed to the right of the existing input
channels (phrases), after buses / FX returns.

**Sirius's Output Mixer alone decides which physical outputs each OTTO
channel reaches**, via the same per-channel main-out destination picker
every other Output Mixer strip already uses. OTTO does not own
physical-output routing inside Sirius — its
`OutputRouter::Mode` (`StereoSum` / `PerPlayer` / `PerDrum`) is OTTO's
internal business and is **rejected as an app-wide Sirius concept**.

**Default fanout follows the audio interface.** A 2-out interface →
1 phrase + 32 OTTO channels sum to outs 1–2. A 16-out interface →
the operator parks the phrase on 1–2 and places any OTTO channel on
any other available output pair in any combination.

This realizes and slightly extends `project_io_ownership_direct_layer`:
the existing rule says "output mixer owns ALL physical outputs"; this
decision adds that **a bundled software instrument (OTTO) does not own
its own physical-output routing inside Sirius either** — it delivers
stereo streams to the Output Mixer, which routes from there.

### Tape vs render/export carve-out

- **Tape path** (live performance loop) — Input Mixer → tape. OTTO is
  **NOT** in this path. Tapes are the live looper's record of the
  performer; OTTO is just bundled-instrument accompaniment heard in
  the room, not recorded to tape.
- **Render / export path** (offline bounce — `prune` and/or `export` to
  WAV/AIFF) — Output Mixer → file(s). OTTO **IS** in this path
  automatically, since OTTO is an Output Mixer source. A project that
  had OTTO playing along will naturally have OTTO's contribution in
  the exported audio.

The render-pipeline stems-vs-master export detail is **deferred** to
the parked render-pipeline brainstorm (see memory
`project_render_to_parts_timeline`).

### Implementation scope (downstream of this spec)

Lands with **P8 (Output Mixer UI)** and the **input→output bridge
slice**. Both are already on the roadmap; this decision constrains
what those slices must accommodate (32 additional channel strips +
explicit per-channel physical-output routing). No new engine concept;
the existing Output Mixer channel + destination picker apparatus
already supports it.

---

## Decision 2: Cross-project inbox protocol (already live)

**Goal: zero friction between Sirius's Claude and OTTO's Claude.**
Operator is **NOT** in the back-and-forth loop; AI-to-AI handoff is
the entire mechanism.

**Sirius has full edit autonomy on OTTO source**, with three mandatory
layers of awareness propagation:

1. **`external/OTTO/CROSS_PROJECT_INBOX.md`** (file, in OTTO repo) —
   single async message channel with `[FROM SIRIUS → OTTO]` and
   `[FROM OTTO → SIRIUS]` sections. Entries have a structured format
   (subject, direction, sirius-sha + otto-sha, files, why, recipient
   guidance, status, resolution).
2. **Git commit trailers** — `Sirius-Origin: <sirius-sha>` on
   Sirius-originated OTTO commits; `OTTO-Origin: <otto-sha>` on
   Sirius commits that consume OTTO changes (e.g., submodule bumps).
   Forever-durable audit trail in git log.
3. **Standing rule in both `CLAUDE.md` files** — identical clause:
   "At session start, read `CROSS_PROJECT_INBOX.md`. Acknowledge any
   unacknowledged entries addressed to you. When making cross-project
   edits, append a new entry + use the appropriate `Origin:` trailer.
   The operator is NOT a required reviewer."

**Push authority:** Sirius's session commits AND pushes the OTTO
change to `origin/main` as part of the same workflow that bumps the
submodule SHA on the Sirius side. Extends
`feedback_claude_commits_and_pushes_master` to OTTO via this protocol.

**Not used and why:**
- `continue.md` — session state, gets refreshed each session; cross-
  project entries would collide.
- `todo.md` — for deferred *work*; cross-project edits are *complete*.
- `CLAUDE.md` itself — right home for the standing rule, wrong home
  for per-incident log entries (would bloat).

### Status: LIVE (bootstrap shipped this session)

- OTTO commit `abf8e4d4` (`docs: bootstrap cross-project inbox protocol
  with Sirius (Sirius-Origin: 75c6866)`) — created
  `CROSS_PROJECT_INBOX.md` + added the standing rule to OTTO's
  `CLAUDE.md` + backfilled 3 entries for this session's prior OTTO
  commits (`94a9c054`, `3f535a53`, `6b066db2`), all marked
  `Status: acked 2026-05-22`.
- Sirius commit `42cb1a9` (`docs: bootstrap cross-project inbox
  protocol + bump OTTO submodule 6b066db2→abf8e4d4 (OTTO-Origin:
  abf8e4d4)`) — added the standing rule to Sirius's `CLAUDE.md` +
  rewrote the stale "Sister app: OTTO" section + bumped submodule.

---

## Decision 3: Internal-FX adapter architecture (T3 blueprint)

Sirius ships internal EQ / CMP / RVB / DLY as first-class product
(per `project_internal_fx_first_class`), consuming OTTO's header-only
Player FX (`PlayerEQ.h`, `PlayerCompressor.h`, `PlayerIRConvolution.h`,
`PlayerDelay.h` — all under
`external/OTTO/src/otto-core/include/otto/effects/`, all
`juce_dsp`-only dependencies). 3rd-party VST/CLAP hosting is
**additional** to internal FX, not the model.

### Architecture

- **`EffectChainEntry` becomes a variant.** New
  `SlotKind = Empty | Internal | Plugin`; the Internal case carries
  an `InternalFXDescriptor { kind: EQ|CMP|RVB|DLY, paramBlob }`. The
  EffectChain remains a copy-on-write data model with NO runtime
  processor objects — preserves the M5 pattern.
- **`IEffectChainHost::pumpSlot()` stays the single audio-thread
  dispatch surface.** Inside the impl: Plugin slots take the existing
  out-of-process IPC path; Internal slots take a local adapter
  lookup. No new audio-thread API; no new dispatch path on the hot
  loop.
- **Host owns adapter storage** — keyed by `(busId, slotIdx)`.
  Adapters created/destroyed on the message thread when the
  EffectChain config mutates.
- **`InternalFXAdapter` wraps one OTTO Player FX as a member.** E.g.
  `struct EqAdapter { otto::effects::PlayerEQ eq_; ConfigSwap<...> cfg_; ... };`.
  Adapter exposes `prepare(sampleRate, blockSize)` (message thread,
  delegates to Player FX's `prepare()`); `process(buffer)` (audio
  thread, `noexcept`, acquire-loads live config, delegates to Player
  FX's `process()`); and a config-swap setter API on the message
  thread.
- **Parameters use config-swap** (UI writes scratch → release-store
  commit; audio thread acquire-loads `liveConfig()` once per buffer).
  Matches OTTO's `MasterBus` precedent exactly. Zero per-parameter
  atomic overhead in the hot loop; parameters coherent at buffer
  boundaries.

### Sequencing

**EQ → CMP → DLY → RVB sequential.** The three pure-DSP cases first;
the adapter pattern is proven before tackling RVB's background-thread
IR-loading complexity (the `PlayerIRConvolution` async double-buffer
swap pattern) and the asset-path step (see Decision 4).

### Plugin-scanner caveat (T5)

Per `project_plugin_scanner_broken`, the host plugin scanner GUI-locks
on Scan today and is broken at all entry points (now Bug 2 in
`todo.md`). T5's insert-chain UI ships **internal-FX-only** with a
greyed-out "VST/CLAP plugin… (scanner unavailable)" entry until the
scanner is fixed (provisional slice "P7-scanner").

### Alignment check

- ✅ `project_internal_fx_first_class` — first-class internal-FX
  product, juce_dsp-only deps.
- ✅ `docs/RT_SAFETY_CONTRACT.md` — OTTO Player FX already comply;
  config-swap is RT-safe.
- ✅ `EffectChainSlotKind` union — already on the T2 docket.
- ✅ CoW chain model — chain stays lightweight data; host owns runtime.

---

## Decision 4: Asset policy — full access via shared bundling

**Sirius has access to EVERYTHING OTTO has** — IRs, Samples, patterns,
fonts, GUI, models, all of it. Nothing excluded, nothing curated.
This realizes `project_sirius_branding_and_otto`: Sirius's installer
bundles full OTTO with paywall enforcement at the feature level
(runtime gate), not at the asset/binary level. All bits are present
on the customer machine.

### The actual OTTO asset tree

(Verified at `/Users/larryseyer/AudioDevelopment/OTTO/assets/` —
**not** the sparse submodule view, which is gitignored.)

- `/IR/` — 200 MB, ~190 subdirectories of impulse responses.
- `/Sampler/` — 3.4 GB. 6212 FLAC + 792 WAV + 128 AIF + dozens of
  `.sfz` mappings.
- `/GUI/` — 61 MB graphics.
- `/Fonts/` — 9 MB (more than the two tracked PCv6 families).
- `/data/` — 64 KB (incl. `genre_tables.bin`). `/models/` — 4 KB.

Total **~3.7 GB**. None of it rides the submodule.

### Implementation

- **Dev time:** Sirius's CMake takes an `OTTO_ASSETS_DIR` variable
  defaulting to `/Users/larryseyer/AudioDevelopment/OTTO/assets/`.
  Sirius's code reads OTTO assets from there directly — no duplication
  into Sirius's repo. CI / other dev machines override as needed.
- **Customer install time:** The installer / .app-bundling pipeline
  copies the OTTO asset tree into one shared location inside the
  install bundle (e.g.
  `Sirius Looper.app/Contents/Resources/Assets/`). Both bundled-OTTO
  and Sirius read from the same path at runtime. Customer machine
  carries **one** copy.
- **Sirius's repo `assets/` directory:** stays empty (or only carries
  Sirius-specific assets that aren't part of OTTO's tree). No 3.7 GB
  duplication in source control.

### License-model parity

`project_sirius_branding_and_otto` records "Licensing IDENTICAL."
Reinforced this session: Sirius's license model — both the legal
documents AND the runtime paywall infrastructure — should match
OTTO's exactly.

- **Legal docs:** Sirius's `LICENSE`, `LICENSE-THIRD-PARTY.md`,
  `SAMPLE-LICENSE.md`, `licenses/` should mirror OTTO's structure
  and content, with product-name substitution where needed.
- **Runtime paywall:** mirror OTTO's entitlement-check code shape,
  activation flow, keychain/license-server integration. Single
  shared implementation pattern; bundled-OTTO inside Sirius and
  Sirius itself both use the same infrastructure.
- **Asset redistribution:** identical license = identical
  redistribution rights → no licensing risk in bundling OTTO's full
  asset set inside Sirius's installer.

### T3-RVB unblocked

T3-RVB's IR sourcing question is closed by this decision: RVB code
points at `${OTTO_ASSETS_DIR}/IR/<selected-subdir>/...` the same way
OTTO's `PlayerIRConvolution` does. No external CC0 IR sourcing
needed; no per-IR licensing review.

---

## Already-shipped: Bus::process meter coupling fix

Operator surfaced a real engine bug mid-brainstorm: bus meter dead on
direct-out destination, working on tape destination. Root cause:
`Bus::process` early-returned on `numChannels <= 0`, which is the
shape used by `InputMixer::renderInputGraph` at line 738 when
`numDirectOutChannels == 0` (the P8 input→output bridge slice that
would supply a real output buffer is parked). The early-return
skipped metering along with output writeback.

Fix shipped as commit `75c6866`: decoupled metering width (uses bus's
own configured `channelCount` when caller's output is unusable) from
output writeback (per-channel-guarded, silently skipped when no usable
destination). New regression test
`[regression-2026-05-22]` in `tests/BusTests.cpp` covers all three
no-output-buffer shapes (null pointer; `dp[]` of nullptrs; sustained
LUFS over no-output buffer).

---

## Verification

- ✅ ctest `[bus]` / `[input-mixer]` / `[output-mixer]` / `[meter]`
  green after meter fix (Phase 1 done).
- ✅ Cross-project inbox protocol live in both repos
  (OTTO `abf8e4d4`, Sirius `42cb1a9`).
- ✅ OTTO `CROSS_PROJECT_INBOX.md` carries 3 backfilled entries
  (Status: acked 2026-05-22).
- ⏳ Decision 1 (OTTO-as-Output-Mixer-source) requires P8 +
  input→output bridge slice; constrains those designs.
- ⏳ Decision 3 (internal-FX adapter) is the T3 slice blueprint.
- ⏳ Decision 4 (asset policy) requires `OTTO_ASSETS_DIR` CMake plumbing
  + installer pipeline rework when T3-RVB or installer work runs.