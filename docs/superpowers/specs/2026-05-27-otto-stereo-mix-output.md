# OTTO Stereo Mix output — design plan

**Date:** 2026-05-27
**Status:** spec (not yet planned for implementation)
**Origin:** operator request, 2026-05-27 — "we should also offer a stereo
out from OTTO option ... all players out into a single stereo pair in
addition to each player's stereo out"
**Related memory:** [[project_otto_as_output_mixer_source]],
[[project_otto_is_the_transport_source]],
[[project_otto_is_a_submodule_now]],
[[project_cross_project_inbox_protocol]]
**Related M-OTTO milestones:** sits on top of slices 1, 2, 4a, 4b (the
M-OTTO-4 engine + picker UI work); independent of slices 4c / 4d.

---

## 1. What

Add one additional Output-Mixer source from OTTO — a **stereo master
sum** of OTTO's four `PlayerOut1..4` sub-buses (output indices 28..31),
exposed in the same "Add OTTO source ▶" picker as a 33rd entry called
**"OTTO Stereo Out (master mix)"**.

When selected, the operator gets a single stereo strip carrying the
sum of all four player buses — the audio that would come out of OTTO's
standalone if it were running in `OutputRouter::Mode::Stereo`. This is
the simplest mental model for a user who doesn't want to think about
per-instrument routing.

The 32 per-output strips (slice 4b) remain unchanged and continue to
co-exist with the new stereo strip. They are not mutually exclusive —
the operator may add the stereo mix AND any subset of the 32 per-output
strips at the same time, with the obvious caveat that summing the
stereo mix alongside the player buses double-counts those signals.

The picker policy is: "Stereo Out" is always offered; per-output entries
are filtered exactly as today. We do not silently exclude per-output
entries when the stereo mix is added (or vice versa) — the operator is
trusted to make the routing call they want.

---

## 2. Why

- **Default-out is the common case.** Most users will just want one
  stereo strip representing "OTTO" — they don't care about the 32-
  output topology. Forcing them to add four `PlayerOut1..4` strips and
  understand they need all four for the full mix is friction.
- **Aligns with OTTO's own architecture.** OTTO's `OutputRouter::Mode`
  has a `Stereo` mode where everything sums to a single stereo pair.
  Surfacing that mode as a single picker entry mirrors OTTO's own
  product design without re-implementing it.
- **Cheap to add.** The per-player buses already exist (slice 1).
  Summing four stereo pairs at the end of `OttoHost::renderBlock` is
  ~256 float adds per block (4 buses × 2 channels × N samples; N
  typically 64–512). Negligible against OTTO's render cost.

---

## 3. How — three approaches

### Option A — OTTO-side accessor (cross-project change)

Ask OTTO's Claude (via the inbox) to expose a
`GlobalMixer::getMasterOutputLeft/Right()` method that returns a
pointer to OTTO's existing master-bus buffer (already computed by
`processGlobalMixer` if OTTO maintains it — verify) or a newly-added
internal buffer summed during `processGlobalMixer`.

- **Pros:** Cleanest semantically — OTTO owns its master-sum behaviour,
  IDA just consumes a pointer. Future improvements to OTTO's master
  bus (mastering plugins, etc.) propagate to IDA automatically.
- **Cons:** Requires cross-project change + inbox round-trip. The
  current `processGlobalMixer` path explicitly does NOT compute a
  master sum (that's `processBlockWithMasterBus`'s job). Asking OTTO to
  ALSO compute a master sum inside `processGlobalMixer` is a behaviour
  change for OTTO's other consumers.
- **Risk:** Wait time on OTTO's Claude. Also: OTTO might counter-
  propose its own architecture (e.g. "use processBlockWithMasterBus
  instead"), which would block IDA on a larger restructure.

### Option B — IDA-side sum in `OttoHost::renderBlock` (recommended)

In `OttoHost::renderBlock(numSamples)`, after the existing
`processGlobalMixer` call, sum the four `PlayerOut1..4` buses
(indices 28..31) into an internal stereo pair owned by OttoHost.
Expose via two new accessors that mirror the existing per-output
accessor shape:

    const float* getOttoStereoMixLeft()  const noexcept;
    const float* getOttoStereoMixRight() const noexcept;

- **Pros:** Zero OTTO-side change. Single render pass per block. IDA
  owns the sum policy (could change the source set later — e.g. sum
  the 24 instrument channels instead of the 4 player buses — without
  needing OTTO to cooperate). The internal stereo buffer lives in
  OttoHost's stable per-prepare allocation, so the source-pointer
  contract from slice 4a (stable for OttoHost's lifetime,
  `setChannelAudioSource` is set-once) extends naturally to this new
  pair.
- **Cons:** IDA duplicates the semantic of "OTTO's master sum." If
  OTTO ever changes what `OutputRouter::Mode::Stereo` does (e.g.
  applies a master limiter), IDA's stereo mix will diverge unless
  someone notices and updates OttoHost.
- **Mitigation:** Comment the sum routine pointing at OTTO's
  `OutputRouter::Mode::Stereo` reference behaviour. If OTTO later adds
  meaningful master processing, file an inbox entry to migrate to
  Option A then.

### Option C — Call `processBlockWithMasterBus` (rejected)

Render OTTO twice per block — once via `processGlobalMixer` for the
per-output strips, once via `processBlockWithMasterBus` for the master
sum.

- **Cons:** Double CPU cost. OTTO's render is not free (sfizz voices,
  per-channel FX). Doubling for a single bonus strip is wasteful and
  scales with operator OTTO load.
- **Verdict:** Reject. Listed only for completeness.

### Decision

**Option B.** Ships now without OTTO-side coordination. If a future
behaviour need surfaces, migrate to A via inbox.

---

## 4. Implementation slices

Plan: ~2 commits, both IDA-only.

### Slice S1 — `OttoHost` stereo-mix sum + accessors + tests

`otto-bridge/include/ida/OttoHost.h` gains:

- Two new accessors, named in parallel to the per-output ones:

      const float* getOttoStereoMixLeft()  const noexcept;
      const float* getOttoStereoMixRight() const noexcept;

  Both return nullptr until `prepare()` has run; after `prepare()`
  they are stable for the OttoHost's lifetime (same contract slice 1
  established for per-output pointers).

- A constant `static constexpr int kOttoStereoMixSentinelIndex = -2;`
  documented next to `kNumOttoOutputs`. **Why -2:** the existing
  `addOttoOutputStrip(int ottoOutputIndex)` validates against
  `[0, kNumOttoOutputs)`; using a negative sentinel lets the stereo
  mix flow through a SEPARATE method without polluting the
  per-output index space. (Alternative: bump kNumOttoOutputs to 33 —
  rejected because it breaks `static_assert (kNumOttoOutputs == 32,
  ...)` invariants in slice 1's tests and the matching OTTO constant.)

`otto-bridge/src/OttoHost.cpp`:

- Allocate `stereoMixL_`, `stereoMixR_` (single `std::vector<float>`
  pair sized to the prepared block size — same pattern OTTO uses for
  its own per-output buffers). Allocated in `prepare()`, never resized
  during render.

- In `renderBlock(numSamples)`, AFTER the existing
  `mixer.processGlobalMixer(numSamples)` call, sum the four
  `PlayerOut1..4` buses (indices 28..31 via the existing
  `mixer.getPlayerOutputLeft/Right()` accessors) into
  `stereoMixL_/R_` for `numSamples` samples. Pure float adds, no
  allocations, no locks — RT-safe per the contract.

- `getOttoStereoMixLeft/Right()` return raw pointers to the buffers.

`tests/OttoHostStereoMixTests.cpp` (new, `[otto-host-stereo-mix]` tag):

- Pre-`prepare`: accessors return nullptr.
- Post-`prepare`: accessors return non-null + stable across renderBlock
  calls (same pointer-stability assertion slice 1 made for per-output).
- A no-input render produces zero-magnitude output (sum of zeros).
- A synthetic test fills OTTO's four PlayerOut buses with known
  signals (e.g. PlayerOut1 = +1.0, others = 0) and asserts the
  stereo mix equals +1.0 (sample-wise) — proves the sum is wired to
  the right buses. Tricky bit: OTTO doesn't expose a "write to player
  bus" hook for tests; this test may need to drive a synthetic SFZ
  voice through OTTO to populate PlayerOut1 indirectly. If that's
  intractable, settle for "the stereo mix equals the per-sample sum
  of the four `getPlayerOutputLeft/Right()` pointers" — a direct
  invariant on the sum routine, which is cheaper to verify.

Size: small. ~1 commit.

### Slice S2 — Picker "OTTO Stereo Out" entry + `MainComponent::addOttoStereoMixStrip`

`app/MainComponent.{h,cpp}`:

- New method `ida::OutputChannelId addOttoStereoMixStrip();` parallel
  to `addOttoOutputStrip(int)`. Same bracket-the-audio-callback
  pattern, same `OutputMixer::addChannel` + `setChannelStrip` +
  `setChannelAudioSource` cycle, but binds to `ottoHost_
  ->getOttoStereoMixLeft/Right()` instead of the per-output
  accessors. Idempotent on double-call (returns existing chId stored
  in a new `ottoStereoMixChannelId_` member).

- Companion `void removeOttoStereoMixStrip();` and
  `bool hasOttoStereoMixStrip() const;`. Mirror of the per-output
  trio.

`app/MainComponent.cpp` — OutputMixerPane (`OutputMixerPane`):

- Picker submenu (`showBlankAreaMenu`) prepends ONE additional entry
  before the per-output list: "OTTO Stereo Out (master mix)".
  Disabled (with a "(already added)" suffix) when
  `hasOttoStereoMixStrip()` is true. Selecting it invokes a new
  callback `onAddOttoStereoMixStrip()` that routes to MainComponent's
  `addOttoStereoMixStrip` and pushes a strip via a new
  `appendOttoStereoMixStrip` on the pane.

- The stereo mix strip uses the same `ottoStrips_` band as the
  per-output strips so the layout grows naturally. Strip `id` =
  `kOttoStereoMixSentinelIndex` (-2) so listener callbacks
  discriminate it from per-output OTTO strips (which use id 0..31)
  AND from the real master (id -1). Pane-side: a small switch on
  `id == -2` routes gain / mute / remove to the stereo-mix
  callbacks; everything else routes to the per-output callbacks.

  Alternative considered: place the stereo-mix strip at a distinct
  position (e.g. leftmost in the OTTO band, or just to the right of
  master). Rejected for slice S2 — equal placement keeps the listener
  dispatch uniform; spatial differentiation can come later as polish.

- Strip name: "OTTO Stereo" (short enough for the strip's name band;
  the picker's "(master mix)" suffix establishes the meaning).

- Right-click / long-press "Remove" works the same way per-output
  strips do, routing to `removeOttoStereoMixStrip` via
  `onRemoveOttoStereoMixStrip`.

Persistence (slice 4c territory): when 4c lands the
`otto_output_strips` JSON envelope, ALSO serialise a boolean
`otto_stereo_mix_strip_present`. Adds one extra field; nothing
structural to invent.

Size: small-medium. ~1 commit.

---

## 5. Naming alignment

`OutputMixerPane::ottoFriendlyName(int idx)` already produces the
operator-canonical names for indices 0..31 (Kick / Snare / ... /
PlayerOut4) as of 2026-05-27. For the stereo-mix entry, the picker
label is **"OTTO Stereo Out (master mix)"** (matches the operator's
phrasing "stereo out from OTTO option"). The strip's own name (the
shorter form rendered on the strip's name band) is **"OTTO Stereo"**.
Both strings are constants in the pane code; no need to extend
`ottoFriendlyName` to handle the sentinel — the stereo mix is
named explicitly at the picker insertion and append-strip sites.

---

## 6. Open questions to resolve at implementation time

1. **What does OTTO's `Mode::Stereo` actually sum?** Slice S1's
   recommendation is to sum `PlayerOut1..4` (the player sub-buses,
   indices 28..31). OTTO's real `processBlockWithMasterBus` may sum
   the 24 instrument channels directly (indices 0..23) AND the FX
   returns (24..27) without going through the player buses, OR may
   sum the player buses, OR may apply a per-instrument-channel pan
   law that the player buses already compose. Before writing slice
   S1's sum routine, audit OTTO's `processBlockWithMasterBus` to
   confirm what the equivalent IDA-side sum should mirror. If
   `PlayerOut1..4` doesn't fully capture the master mix (e.g.
   instrument channels also go direct to master, bypassing player
   buses), the IDA-side sum needs to match that exact accumulation —
   summing only `PlayerOut1..4` would silently miss content.

2. **Should the stereo-mix strip be visually distinct?** The current
   plan places it in the same `ottoStrips_` band alongside per-output
   strips. Alternative: pin it to a position next to master (it IS a
   "master-like" tap conceptually), separated by a divider gap. Decide
   at S2 time; not load-bearing.

3. **What happens if both stereo mix AND any per-output strip are
   added?** Routing-wise nothing breaks — they're independent
   OutputMixer channels. But the operator gets a double-count: the
   stereo mix already contains PlayerOut1, and a separate PlayerOut1
   strip adds its own copy on top. Mention this in the picker tooltip
   or in the user guide; do not enforce mutual exclusion.

4. **iOS picker UI viability.** Per `feedback_ios_long_press_pairs_
   right_click`, the picker is already iOS-friendly via long-press.
   The submenu now has 33 entries — fits comfortably in OTTO's
   `TouchScrollableMenuPopup` (which handles longer lists natively).
   No change needed.

5. **OTTO Mode::Stereo + IDA stereo-mix coexistence.** When OTTO is
   running its own internal `OutputRouter::Mode::Stereo`, does
   `processGlobalMixer` still populate the per-output buffers? If
   not, IDA's stereo-mix sum would read stale/empty data. Verify in
   slice S1 — likely OTTO's GlobalMixer always populates
   per-output regardless of OutputRouter mode (the modes are
   downstream of the global mixer), but worth confirming.

---

## 7. Sequencing

Land slice 4d (transport-start) and 4c (per-output routing +
persistence) FIRST — those finish M-OTTO-4 as currently scoped. Land
the stereo mix AFTER. Rationale: the stereo mix is a UX
simplification on top of an already-working slice 4 — without 4d the
stereo mix is silent (no transport rolling), without 4c the stereo
mix can't route to a specific hardware output pair. Putting the
stereo mix BEFORE those would ship another silent / unrouteable strip.

The plan is intentionally written so slice S1 + S2 can be picked up
without re-deriving the design. When 4c + 4d are in, this doc is the
single source of truth for picking up the stereo mix work.

---

## 8. Files touched (estimate)

S1:
- `otto-bridge/include/ida/OttoHost.h` — add accessors + sentinel
  constant + buffer declarations
- `otto-bridge/src/OttoHost.cpp` — buffer allocation + sum routine
- `tests/OttoHostStereoMixTests.cpp` — new test file
- `tests/CMakeLists.txt` — register the new test

S2:
- `app/MainComponent.h` — `addOttoStereoMixStrip` /
  `removeOttoStereoMixStrip` / `hasOttoStereoMixStrip` declarations +
  `ottoStereoMixChannelId_` member
- `app/MainComponent.cpp` — implementations + `OutputMixerPane`
  changes (picker entry, listener dispatch, gesture relays,
  appendOttoStereoMixStrip)

No OTTO-side files. No submodule bump.
