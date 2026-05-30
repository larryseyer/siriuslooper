# Tape saturation — the Jiles-Atherton model

The tape nonlinearity in IDA (and in the OTTO TAPECOLOR module IDA consumes) is a
**Jiles-Atherton magnetic-hysteresis solver** — the Chow Tape Model lineage
(Chowdhury, DAFx-19), an NR4 fixed-point trapezoidal step integrated in the H
(field) domain so the sample period cancels. It is the **one and only**
tape-saturation algorithm. The earlier static `asinh` waveshaper was removed; the
operator adopted J-A by ear over a level- and FR-matched A/B.

Source of truth: `external/lsfx_tapecolor/dsp/HysteresisProcessor.{h,cpp}`. The
OTTO copy at `external/OTTO/src/otto-core/.../tapecolor/HysteresisProcessor.{h,cpp}`
is a byte-faithful mirror (namespace + include swap only).

## Why a hysteresis model, not a waveshaper

A memoryless waveshaper (`asinh`, `tanh`) maps each sample independently — it has no
memory. Real tape has memory: magnetisation depends on the field *history*, tracing
minor loops on transients and program-dependent saturation. J-A models that history
directly (magnetisation `M` as a function of the applied field `H` and its rate of
change), so the colour it adds is the program-dependent loop behaviour of tape, not a
fixed transfer curve.

## Coefficient set and operating point

The solver runs in a **normalised coefficient set** (`Ms ≈ 1`, so `M` and `H` stay
O(1) — far better conditioned than the literature's 1e5–1e6 SI magnitudes, with an
identical loop shape):

| Coefficient | Value | Meaning |
|---|---|---|
| `kJaMs`    | 1.0    | saturation magnetisation |
| `kJaA`     | 0.40   | anhysteretic shape (knee width) |
| `kJaAlpha` | 1.6e-3 | inter-domain mean-field coupling |
| `kJaC`     | 0.70   | reversibility fraction |
| `kJaGin`   | 1.0    | audio `x` → `H` field input scale |
| `kJaIters` | 4      | fixed-point iterations per sample (NR4) |

**Operating point — the knee sits above program.** `Qfull = Gin·Ms/a ≈ 2.5`: full
scale lands in the *gentle pre-saturation region*, not deep in Langevin clip, the same
way a calibrated tape machine keeps normal program below the compression knee. Through
the working range the stage is unity; only material above the knee is softened.

## Peak-safety by construction (no dynamic makeup)

The stage is level-matched **by construction** and **never expands a peak**. Three
properties enforce this:

1. **Unity small-signal gain via `normGain_`.** The raw J-A slope `c·Ms/(3a)` sits
   *below* unity, so the core would darken quiet detail. The effective small-signal AC
   gain is **measured once at `prepare()`** (a single-bin DFT of the fundamental at a
   deep-linear `1e-4` probe — fundamental gain, rejecting the harmonics the loop adds as
   *character*, not attenuation) and its reciprocal stored as `normGain_`. Quiet detail
   then passes at exactly unity. This is the load-bearing fix for the 2026-05-25
   "dull/dark" failure — that symptom was the un-normalised slope, not the model.

2. **No dynamic makeup — ever.** There is no broadband-RMS level-match in `process()`.
   This is deliberate and is the post-mortem of the J-A overload: an always-on dynamic
   RMS makeup *expanded transient peaks past full scale* (peaks compress more than RMS
   through a saturator, so returning the RMS loss inflated the peaks). On a sharp
   transient that drove the NR fixed-point solver through its near-singular irreversible
   denominator to a finite-but-huge `M`, the full chain emitted a +65 dB peak from a 1.0
   input and overloaded the host. The makeup was removed entirely. Because the knee sits
   above program (point above), the stage needs no makeup — it is already level-matched.

3. **Physical magnetisation clamp `|M| ≤ 1.05·Ms`.** `M` can never physically exceed
   `Ms` (all domains aligned); any excursion past it is numerical error from a sharp
   transient through the near-singular irreversible denominator. Clamping in the solver
   step (`kJaMmax = 1.05 * kJaMs`) makes a finite-but-huge excursion impossible, so the
   stage can never emit a peak the NaN guard would miss. A full-chain peak regression
   test pins this (see `tests/TapeColorChainPeakTests.cpp`).

A non-finite `M` is also caught per-step: rather than just zeroing the output sample,
the per-channel state (`M`, `H`, `dM/dH`) is cleared so a poisoned magnetisation cannot
propagate to subsequent samples.

## Parameter mapping — HYST and SAT

Two front-panel knobs drive the model. Both are `[0..1]`, geometric, centred on noon.

**HYST → Jiles-Atherton loop width (pinning / coercivity `k`).** This is the one
runtime-variable coefficient.

```
jaK_ = kLoopWidthDefault * pow(2, (hyst - 0.5) * 2)
```

| HYST | `k` | Character |
|---|---|---|
| 0.0 | 0.06 | tighter loop — cleaner |
| 0.5 | 0.12 (`kLoopWidthDefault`) | the operator-approved default voicing (bit-identical to the prior fixed value) |
| 1.0 | 0.24 | wider loop — more hysteresis colour |

The deep-linear small-signal slope is **`k`-independent** (the irreversible `f1` term
vanishes as amplitude → 0), so the unity `normGain_` measured once at the default `k`
holds across the *entire* HYST sweep; `k` only widens or narrows the loop at finite
signal — the audible character. The `Ms` clamp keeps every setting peak-safe. A
regression test pins unity + full-scale peak ≤ 1.05 across HYST 0/.25/.5/.75/1 (see
`tests/HysteresisProcessorJATests.cpp`).

**SAT → matched drive / inverse-drive pair around the solver.** Hotter drive pushes
harder into the hysteresis knee; the matched inverse restores level.

```
driveGain_    = pow(2, (sat - 0.5) * 2)      // 0 → 0.5× (−6 dB), 0.5 → 1×, 1 → 2× (+6 dB)
invDriveGain_ = 1 / driveGain_
```

The signal path per sample is `x → ·drive → J-A solver → ·normGain → ·invDrive`.

## Audio-thread contract

`process()` and everything it calls are alloc-free, lock-free, log-free, and `noexcept`.
Per sample: 4 fixed-point iterations, each a Langevin (`coth(x) − 1/x`, with a Maclaurin
series below `|x| ~ 1e-4` to dodge the `coth` singularity and catastrophic cancellation)
plus a few transcendentals. Parameters are captured at `updateParameters()` on the
message thread; `normGain_` is computed once at `prepare()`. The caller
(`TapeColorProcessor`) owns any oversampling — `process()` runs on the already-upsampled
buffer.
