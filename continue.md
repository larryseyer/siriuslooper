# Session Continuation — Jiles-Atherton ADOPTED + overload FIXED + shipped across all 3 repos (2026-05-30)

## ▶ 0. TL;DR (read first)

Operator auditioned the fixed `TAPECOLOR AB` VST3 and gave the verdict: **"Jiles-Atherton is FAR superior —
that's the one we keep."** J-A is now the adopted tape-saturation model; **asinh stays as the selectable
fallback**. The instant-overload bug from the first audition is fixed, headless-verified, and **committed +
pushed** across lsfx and OTTO; the IDA commit is the last step (in progress this session).

**Pushed SHAs:** lsfx_tapecolor `df3c4da → 55b78e1` (origin/main) · OTTO `79d665ec → b081a71c` (origin/main,
`Ida-Origin: ef1a7cc`) · IDA → origin/master (pins bumped to the two above).

There is **no open J-A work.** Coefficients can still be tuned by ear later (`kJaA`/`kJaGin`) but the design is
settled and shipped.

## ▶ 1. What happened this session

1. **Bug:** enabling J-A on the `TAPECOLOR AB` VST3 in Reaper instantly + persistently overloaded the track
   (protective mute). The operator never heard the character.
2. **Root cause (proven by a new full-chain test):** the shared dynamic broadband-RMS level-match
   **expanded transient peaks past full scale** (peaks compress more than RMS through a saturator, so handing
   the RMS loss back inflated the peaks), and a sharp transient drove the NR fixed-point solver through its
   near-singular irreversible denominator to a finite-but-huge M. The test caught **+65 dB** out from 1.0 in.
3. **Fix (3 parts):** a saturation stage must be level-matched **by construction**, never by a
   transient-expanding makeup — (1) re-calibrated operating point `kJaA 0.10→0.40` (Qfull≈2.5, knee above
   program, like asinh + real tape), (2) **removed the dynamic RMS level-match entirely**, (3) added a
   physical magnetisation clamp `|M| ≤ 1.05·Ms` in the solver step.
4. **Verdict:** operator re-auditioned the rebuilt VST3 → **keep J-A.**
5. **Shipped:** lsfx fix committed/pushed; mirrored to OTTO-core (sed namespace-swap, verified diff =
   namespace + include only) + inbox entry, committed/pushed with `Ida-Origin`; IDA pins bumped + tests/docs
   committed/pushed.

## ▶ 2. Code state (all committed)

| Repo | What landed |
|---|---|
| **lsfx_tapecolor `55b78e1`** | `dsp/HysteresisProcessor.{cpp,h}`: Qfull≈2.5, dynamic RMS makeup removed, `\|M\|≤1.05·Ms` clamp, `normGain_` kept, NaN guard kept. `updateParameters` signature UNCHANGED (`effectiveSampleRate` retained, `juce::ignoreUnused`). |
| **OTTO `b081a71c`** | otto-core mirror of the above (namespace-swap) + `CROSS_PROJECT_INBOX.md` FIX entry (supersedes the old always-on-makeup note). OTTO's own `external/lsfx_tapecolor` gitlink intentionally NOT bumped — OTTO compiles the otto-core mirror copy (per `SharedPluginSources.cmake`), as the prior mirror commit did. |
| **IDA (this session's commit)** | `tests/TapeColorChainPeakTests.cpp` (new full-chain peak-safety regression) + wired in `tests/CMakeLists.txt`; `tests/HysteresisProcessorJATests.cpp` (peak-safe asserts replace the removed-makeup asserts; +18 dB ceilings tightened to ≤+0.4 dB); spec `docs/superpowers/specs/2026-05-30-jiles-atherton-ab-experiment.md` (Status → RESOLVED/adopted); both submodule pins bumped (lsfx 55b78e1, OTTO b081a71c). |

## ▶ 3. Verification (headless, all green)

- `tests/TapeColorChainPeakTests.cpp` — full `TapeColorProcessor` chain (oversampling + record-emphasis +
  Level +3 pre-drive) on high-crest program, BOTH models: output peak ≤ input peak +0.5 dB. RED +65 dB before
  fix → GREEN ≤+0.4 dB after.
- `tests/HysteresisProcessorJATests.cpp` — 11 cases incl. rewritten "peak-safe at full scale".
- `[tapecolor-chain],[hysteresis-ja],[tapecolor-adapter]` = 16 cases / 11234 assertions green.
- Full `ctest` = 830/831 (`#831` = documented separately-run editor exe; `#757` FileInputSource = pre-existing
  timing flake, passes 3/3 isolated, unrelated). IDA app builds clean.
- `TAPECOLOR AB` VST3 + AU rebuilt with the fix and installed to `~/Library/Audio/Plug-Ins/{VST3,Components}/`.

## ▶ 4. Naming note (operator asked)

Algorithm = **Jiles-Atherton** (Jiles & Atherton 1986, magnetic hysteresis). **"Chow"** = Jatin Chowdhury's
CHOW Tape Model (DAFx-19), which applies J-A to tape with a real-time solver — the approach we followed. Ours
is J-A-derived, our own voicing (recalibrated, peak-safe), not bit-faithful to CHOW.

## ▶ 5. OTTO inbox

`[FROM IDA → OTTO]`: the FIX entry is **needs-ack** — OTTO's next session reviews it (key point: the
always-on level-match is GONE, so OTTO standalone's asinh tone is now closer to pre-experiment, not further).
The 2026-05-27 EventBus lock-free brief is still `needs-ack` (OTTO next-pass, not blocking IDA).
`[FROM OTTO → IDA]`: empty.

## ▶ 6. Carry-forwards (todo.md)

- **S6 T8** — OTTO output-strip DEST picker save→quit→reload round-trip never operator-completed.
- **Codify the two-terminal protocol in IDA's CLAUDE.md** (still open).
- **~22% idle audio-thread load** — profile before optimizing; matters for iOS.
- **Intermittent play-button-dead-on-launch** — startup race; low priority.
- **#757 FileInputSource ring-growth timing flake** — passes isolated; pre-existing, not J-A-related.
- **OTTO `WetChain.cpp` stale comments** (J-A/trapezoidal/convolution describe old code) — OTTO-side cleanup.
- **Optional J-A by-ear tuning** — `kJaA`/`kJaGin` in `external/lsfx_tapecolor/dsp/HysteresisProcessor.cpp`;
  the no-makeup + Ms-clamp design is invariant regardless of coefficient choice; re-mirror + 3-repo dance if changed.

---

*Net this session: diagnosed + fixed the J-A Reaper overload (transient-expanding RMS makeup removed; Qfull
10→2.5; physical Ms clamp). New full-chain peak-safety regression proves it (RED +65 dB → GREEN ≤+0.4 dB).
Operator auditioned + ADOPTED Jiles-Atherton. Shipped across lsfx (55b78e1) + OTTO (b081a71c) + IDA
(origin/master). asinh retained as fallback.*
