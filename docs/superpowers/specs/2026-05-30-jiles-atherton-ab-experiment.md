# Jiles-Atherton vs. asinh — tape-saturation A/B experiment

Status: queued (own DSP session). Operator wants to hear the difference and decide by ear.

## Context

OTTO/IDA's tape saturation stage (`HysteresisProcessor`, in
`external/OTTO/src/otto-core/.../tapecolor/`) is currently a **static `asinh`
waveshaper** (`y = asinh(k·x)/k`). It replaced a Jiles-Atherton (Chow Tape Model
NR4-style) magnetic-hysteresis solver on 2026-05-25 because the J-A version
"sounded like crap" — dull and distorted.

That failure is now **diagnosed** (documented in `HysteresisProcessor.h`'s own
header): the J-A small-signal slope is `c·Ms/(3a)`, which for the Chow reference
coefficients is **~13 dB *below* unity**. Un-normalized, it attenuated low-level
detail (the "dull"/"gated" character) and forced the user to overdrive to get
level back (the "distorted" character). It was a **normalization bug, not an
inherent J-A flaw** — Chow's own model normalizes this.

CPU is **not** a constraint: 2026-05-30 measurement on an M4 Max showed the
entire tape chain costs ~0% (audio-thread load was flat ~23% at every quality
tier, 77% headroom). An oversampled Newton J-A is easily affordable on desktop
(tighter on iOS/A13 but manageable — and gated per-instance already).

**Goal:** implement a correct J-A solver as a switchable alternative to `asinh`,
A/B it on real program material at matched loudness, and adopt it **only if the
operator's ears prefer it**. `asinh` stays as the fallback either way.

## Why it might be worth it

`asinh` is **memoryless** — same input always maps to the same output. J-A models
actual magnetic hysteresis: **minor loops, history- and frequency-dependent
saturation, magnetic "lag"** — the genuine "tape memory" that a static shaper
structurally cannot reproduce. Whether that's audibly better for real material is
exactly what this experiment decides.

## Design

1. **Add a model selector to `HysteresisProcessor`.**
   `enum class SaturationModel { Asinh, JilesAtherton };` Keep the existing asinh
   path verbatim as `Asinh`. `process()` branches on the live model.

2. **Implement the J-A NR4 solver (`JilesAtherton` path).**
   - Anhysteretic magnetization `M_an = Ms · L(He/a)`, Langevin `L(x)=coth(x)-1/x`
     (use the `x/3 - x³/45` series near 0 to avoid the `coth` singularity).
   - `He = H + α·M`; irreversible/reversible split with `c`, `k`; Newton-Raphson
     per oversampled sample (the WetChain oversampler already provides the rate;
     `updateParameters` already receives `effectiveSampleRate`).
   - Per-tape coefficient sets (`Ms, a, α, c, k`) selected by `cfg.tape`.
   - **Normalize small-signal gain to unity** — the load-bearing fix. Divide the
     output by the model's small-signal slope `c·Ms/(3a)` (or pre-scale input),
     so quiet signals pass at unity and only the *shape* above the knee differs.
     Verify with a –60 dBFS sine: output level must equal input level.

3. **Runtime A/B toggle (essential — blind ABX needs live switching, not
   rebuilds).** Add `int saturationModel { 0 }` to `WetChainConfig` (0=asinh,
   1=J-A) through the existing config-swap, and surface a control the operator can
   flip live — simplest is a temporary picker in the OTTO native tape editor (or a
   hidden key/MIDI toggle if a full picker is too much for an experiment). It does
   NOT need to ship in the final UI; it's the experiment harness.

4. **Loudness-match the two paths** before comparing. Un-equal loudness destroys
   blind A/B (louder reads as "better"). Match short-term LUFS (the WetChain
   already meters it) or RMS-normalize each path to equal output so the only
   variable is character.

## Implementation order (new session)

1. `superpowers:brainstorming` the per-tape J-A coefficient sets + the exact
   normalization point (input pre-scale vs output post-scale) — get this right,
   it's where the last attempt failed.
2. TDD the solver headless: unity small-signal gain, monotonic, bounded, no NaN,
   stable at 1×/2×/4× oversampling.
3. Wire the runtime model toggle + loudness-match.
4. Clean build, **operator blind A/B** on real material (drums, full mix).
5. Decide: keep J-A (optionally retire asinh or keep both as a user choice), or
   revert to asinh-only. Operator's ears are the sole acceptance criterion.

## Coordination (cross-project)

`HysteresisProcessor`/`WetChain` are **OTTO-core** (OTTO-native, shared with IDA
via the submodule). This affects both products. Per `CLAUDE.md` cross-project
protocol: coordinate via `external/OTTO/CROSS_PROJECT_INBOX.md`; OTTO's terminal
owns the TAPECOLOR DSP line. If IDA's terminal does the work while OTTO's is idle,
follow the full inbox + `Ida-Origin:` trailer + pin-bump dance.

## Success criteria

- A correct J-A solver exists behind a runtime toggle, with **unity small-signal
  gain** (the –60 dBFS unity-level test passes — proves the 2026-05-25 bug is fixed).
- Headless tests pin numerical stability across oversampling tiers.
- The operator can flip asinh ↔ J-A live at matched loudness and judge by ear.
- A decision is recorded (J-A adopted / asinh retained), with the reasoning.

## Reference

- `external/OTTO/src/otto-core/include/otto/effects/tapecolor/HysteresisProcessor.h`
  — current asinh stage + the header comment documenting the `c·Ms/(3a)` failure.
- `external/OTTO/src/otto-core/src/effects/tapecolor/WetChain.cpp` — the
  oversampling host (note: its inline comments still say "J-A/trapezoidal/Talpha"
  and "convolution" — both describe deleted code; trust the asinh stage, not the
  comments; a comment-cleanup is logged separately in `todo.md`).
- Chow, J. "Real-Time Physical Modelling of the Tape Recorder" (DAFx-19) — the
  reference J-A formulation and its normalization.
