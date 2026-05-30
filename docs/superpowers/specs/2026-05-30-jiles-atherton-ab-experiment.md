# Jiles-Atherton vs. asinh — tape-saturation A/B experiment

Status: **RESOLVED — Jiles-Atherton adopted (operator verdict by ear, 2026-05-30),
then made the ONLY model.** The A/B is over: `asinh` was removed entirely and the
`SaturationModel` selector deleted. See the closing note below.

## Verdict + the overload fix (2026-05-30)

The first audition build **overloaded instantly** in Reaper (protective mute) — the
operator never heard the character. Root cause: the A/B harness's shared dynamic
broadband-RMS level-match **expanded transient peaks past full scale** (peaks
compress more than RMS through a saturator, so returning the RMS loss boosted the
peaks), and a sharp transient drove the fixed-point solver through its near-singular
irreversible denominator to a finite-but-huge magnetisation. A full-chain peak test
caught a **+65 dB** output peak from a 1.0 input.

The fix — a saturation stage must be level-matched **by construction**, never by a
transient-expanding makeup:
1. Re-calibrated the operating point: `kJaA 0.10 → 0.40` (Qfull = Gin·Ms/a ≈ 2.5,
   not 10) — gentle, knee above program, like asinh and real tape.
2. **Removed the dynamic broadband-RMS level-match entirely.** Both models are now
   level-matched by construction (gentle, non-expanding saturation). Residual
   constant loudness offset is the manual OUT trim knob's job.
3. Added a physical magnetisation clamp `|M| ≤ 1.05·Ms` inside the solver step —
   makes the numerical transient blow-up impossible.

After the fix the operator auditioned the rebuilt `TAPECOLOR AB` VST3 and judged
**"Jiles-Atherton is FAR superior — that's the one we keep."** Regression guard:
`tests/TapeColorChainPeakTests.cpp` pins output peak ≤ input peak + 0.5 dB on the
full oversampled chain for both models (RED +65 dB → GREEN ≤ +0.4 dB);
`tests/HysteresisProcessorJATests.cpp` pins deep-linear unity, flat FR, peak-safety,
no-NaN, and rate-stability. The earlier always-on dynamic level-match is **gone** —
do not reintroduce a makeup that can raise output peak above input peak.

## Closing note — A/B over, J-A is the only model (2026-05-30)

After the operator's "FAR superior" verdict, the experiment harness was dismantled
and J-A made the sole tape nonlinearity:

- `asinh` removed entirely. The `SaturationModel` enum, the `Asinh` branch, and the
  `saturationModel` config field / parameter ID / picker (the "SAT MODEL" control in
  the OTTO native editor + the `TAPECOLOR AB` VST3) are all gone. `grep saturationModel`
  across lsfx + OTTO src + tests is zero.
- The **HYST knob was rewired to the J-A loop width** (`kJaK`, pinning/coercivity),
  geometric around noon: 0.5 → `kLoopWidthDefault = 0.12` (the operator-approved
  voicing, bit-identical), 0 → 0.06 (tighter/cleaner), 1 → 0.24 (wider/more colour).
  The deep-linear slope is `k`-independent, so the unity `normGain_` measured once at
  the default `k` holds across the whole sweep; the `Ms` clamp keeps every setting
  peak-safe. A new test pins unity + peak-safety across the HYST sweep.
- `updateParameters` is now `(float hysteresisAmount, float saturation)` — the
  `tape`/`speed`/`effectiveSampleRate`/`model` args were dropped.

Full model writeup lives at `docs/design/tape-saturation-jiles-atherton.md`.

---

_Original experiment design (kept for the rationale record):_

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
