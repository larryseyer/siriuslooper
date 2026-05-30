# Session Continuation — S6 DEST picker LANDED + crackle saga + OTTO merge; OPEN: TAPECOLOR 1× CPU overload (2026-05-29)

## ▶ 0. TL;DR (read first)

Two big threads this session:

1. **S6 (OTTO output-strip DEST picker + persistence) — T4–T7 all landed + reviewed + pushed.** The
   DEST picker is now operator-verified WORKING (it was inert until a one-line fix — see §2). T8's
   full save/quit/reload round-trip was NOT completed by the operator (the session got consumed by the
   crackle thread). INS-button parity was raised and deferred to its own slice (todo.md, operator chose
   "do it right").

2. **Audio CRACKLE / processor-overload saga (the live open issue).** Routing OTTO to an output made
   OTTO audio audible for the first time, which exposed CPU overload from **per-player TAPECOLOR**.
   Three real RT/perf fixes shipped (denormal guard, per-block MIDI-alloc kill, selective-bus mask),
   plus a cross-project OTTO merge that folded in OTTO's own TAPECOLOR engagement gate + platform-aware
   quality. **Current state: audio is CLEAN with TAPECOLOR disabled, but a SINGLE enabled TAPECOLOR
   instance still overloads** (crackle). This is the open problem for next session — see §4 (it's the
   priority).

ctest **819 passed / 1 not-run** throughout (the 1 not-run is the documented separately-built
MainComponentPluginEditorTests exe). Clean rebuild green on the merged OTTO.

---

## ▶ 1. SHAs at end of session

| Thing | SHA | Note |
|---|---|---|
| IDA HEAD (origin/master) | **52dd5b5** | `chore:` bump OTTO submodule to merged HEAD |
| IDA commits this session (newest→oldest) | `52dd5b5` (OTTO pin bump) · `384e493` (selective-bus) · `4e9dd31` (MIDI-alloc fix) · `e534898` (denormal guard) · `84a9049` (DEST-refresh fix) · `8955539` (T7) · `841b8e2` (T6) · `fc3041a` (T5) · `7ec92d9` (T4 fixup) | all pushed |
| OTTO pin (IDA's submodule) | **0e61a69f** | merged: IDA's activeBusMask + OTTO's TAPECOLOR engagement/quality/config-fix line |
| OTTO origin/main | `0e61a69f` | = IDA's pin (no drift) |
| OTTO commits I pushed | `5b11a84c` (activeBusMask, in 0e61a69f ancestry) · `4187a210` (setSoloButtonVisible, prior) | preserved through OTTO's merge |
| lsfx_tapecolor pin | `0a7189c` | unchanged |

Working tree end-of-session: `m external/sfizz` only (pre-existing 64-ch patch; leave alone).
OTTO submodule is at detached HEAD `0e61a69f` (= origin/main) — fine.

---

## ▶ 2. What shipped this session, in order

### S6 T4–T7 (subagent-driven, reviews per S3c precedent) — all landed + APPROVED
- **T4 fixup `7ec92d9`** — review fixups on the rebind helper (const OttoHost&, channelIdAt form parity, doc). Both T4 reviews SPEC-COMPLIANT + APPROVED.
- **T5 `fc3041a`** — OutputMixerPane OTTO-strip DEST-button scaffolding. Reviews APPROVED (lockstep-vector invariant airtight).
- **T6 `841b8e2`** — wired `onOttoDestinationChosen` + the OTTO loop in `refreshOutputDestinations`. Reviews APPROVED (audio-callback bracketing verified).
- **T7 `8955539`** — `chooseFileAndLoad` calls `rebindOttoChannelsAfterImport` + resets transient OTTO mute/solo on load. Reviews APPROVED. **Corrected a false premise:** `OutputChannelState` has NO `muted` field (output-channel mute is NOT persisted), so the queued "seed ottoStripMuted_ from restored mute" todo was wrong — removed it; the real fix is the transient reset. T7 also surfaced + fixed that `app/OttoStripRebind.cpp` was never in `target_sources(IDA …)` (only the test target) — added it to `app/CMakeLists.txt`.

### DEST-picker bug fix `84a9049` (operator-caught at T8)
- The DEST button opened to NOTHING. Root cause: the live add-OTTO handler `onAddOttoOutputStrip` never called `refreshOutputDestinations()` (unlike the bus-add handler), so `ottoChoices_` stayed empty + the button kept its `"—"` placeholder. Added the refresh to add+remove handlers. **DEST picker now operator-verified working.**

### Crackle fixes (3) — all real RT/perf issues; see §3 for the saga
- **`e534898`** — `ScopedNoDenormals` on IDA's audio callback (was missing entirely; only OttoHost::renderBlock had one). Valid hygiene; NOT the crackle.
- **`4e9dd31`** — reuse a pre-sized `ottoMidiScratch_` member instead of a fresh empty `juce::MidiBuffer` each block (the empty buffer malloc'd on the first `addEvent` of every note-bearing block — per-block audio-thread allocation). Real fix; NOT the dominant crackle.
- **`384e493` + OTTO `5b11a84c`** — **selective-bus mask** (the operator's "only run FX for outputs in use" choice): `GlobalMixer::activeBusMask_` (default all-set → OTTO standalone byte-identical) gates `processBuses` so per-player buses IDA doesn't consume skip their DSP (incl. TAPECOLOR) and emit silence via new `MixerBus::silenceAndGetLeft/Right`. IDA's `OttoHost::setActivePlayerBusMask` + `MainComponent::updateOttoActiveBusMask` (called on strip add/remove/load) derive the mask from `ottoChannelByOutputIndex_`.

### OTTO merge + pin bump `52dd5b5` (cross-project, see §5)
- OTTO's terminal merged origin/main (with my work) + folded in OTTO's own TAPECOLOR line. IDA bumped the pin to `0e61a69f`. All 4 IDA preservation items verified intact (activeBusMask, processBlock split, getConductor, setSoloButtonVisible). OTTO added a NEW audio-thread rule #8 (CPU budget + call-site engagement gate) to its CLAUDE.md.

---

## ▶ 3. The crackle saga (so next session doesn't re-walk it)

- **It was never my routing code on the audio thread** — T5/T6/T7/DEST are all message-thread GUI. Routing OTTO to an output made OTTO audio *audible* (it was silent before S3c; unrouted before the DEST fix), which EXPOSED pre-existing per-block costs.
- **Denormals (e534898)** and **per-block MIDI alloc (4e9dd31)** were real but NOT the operator's crackle.
- **The dominant cause is CPU overload from per-player TAPECOLOR** (operator diagnosed it: disable TAPECOLOR → clean). IDA reads OTTO's per-player bus outputs → forces those bus TAPECOLORs to run; IDA skips OTTO's master mixdown so OTTO's master TAPECOLOR does NOT run in IDA.
- Selective-bus mask + OTTO's engagement gate together get it to **1× TAPECOLOR** for a single PlayerOut strip. **But 1× STILL overloads** — see §4.

---

## ▶ 4. ⚠ OPEN ISSUE (PRIORITY NEXT SESSION): a single TAPECOLOR instance overloads the CPU

**Symptom (operator, end of session):** "It is clean as long as I do not enable tapecolor… As soon as
I enable tapecolor, crackles come back (processor overload)… if tapecolor is disabled, audio is clean."
This is AFTER all the fixes — i.e. even 1× TAPECOLOR is too heavy for the real-time budget.

**What's already ruled out / known:**
- NOT a tiny-buffer problem — IDA's default buffer is already **512** (`MainComponent.cpp` several sites default `bufferSize > 0 ? … : 512`).
- NOT 4× waste anymore — the selective-bus mask + engagement gate confirmed-compose down to 1× for one PlayerOut strip.
- TAPECOLOR is genuinely heavy by design: J-A (Jiles-Atherton) hysteresis **oversampled** + convolution IR + transformer/tube/bias/modulation/noise stages.

**TAPECOLOR quality tiers (from `external/lsfx_tapecolor/dsp/TapeColorProcessor.{h,cpp}`):**
`quality 0 = Eco (1×, NO oversampling)`, `1 = Standard (2×)`, `2 = HQ (4×)`. The oversampler maps 1:1 to
the `TapeColorConfig::quality` field. Per-player **MixerBus runs Standard (quality=1, 2×)**
(`external/OTTO/src/otto-core/include/otto/mixer/MixerBus.h` ctor `tapeColor_->scratchConfig().quality = 1`).
MasterBus runs HQ (quality=2) on desktop after OTTO's platform-aware change (irrelevant to IDA — master
path skipped).

**FIRST thing to try next session (cheap lever, before any DSP rewrite):**
Drop the *consumed* per-player bus TAPECOLOR to **Eco (quality=0, 1× = no oversampling)** and see if 1×
becomes affordable. The oversampling wrap (processSamplesUp + hysteresis + processSamplesDown) is the
biggest single cost; Eco removes it. Levers:
- OTTO-side: `MixerBus` ctor default quality, OR a runtime setter IDA drives (coordinate with OTTO's
  terminal — TAPECOLOR DSP is shared/OTTO territory; do NOT unilaterally rewrite lsfx_tapecolor).
- This is the operator's likely-acceptable middle path: keep tape coloring, just at Eco on the buses.

**If Eco still overloads** → genuine DSP optimization project (coordinate with OTTO + lsfx_tapecolor):
the runtime convolution-IR resample path (see the 2026-05-24 todo "offline pre-bake IRs"), lighter
hysteresis, or skip the convolution stage on buses. This is a real slice, OTTO/lsfx-owned, NOT a quick IDA fix.

**The operator's framing:** "perhaps tapecolor needs to be re-worked to be not so processor intensive."
So they're open to a DSP-cost reduction. But try Eco-tier FIRST (it may fully resolve it for free).

---

## ▶ 5. ⚠ Cross-project coordination — TWO TERMINALS RAN CONCURRENTLY (near-miss)

Mid-session the operator discovered **OTTO's Claude terminal and IDA's Claude terminal were editing the
same OTTO repo at once** (both touched `PluginProcessor.cpp` — auto-merged only because the edits were in
different functions). Resolved safely: OTTO merged (fast-forward, nothing lost), IDA bumped the pin.

**Agreed three-layer coordination protocol (to codify):**
1. **Durable async handoff** = `CROSS_PROJECT_INBOX.md` (keep — it proved its worth: IDA's "do NOT revert"
   guidance is exactly how OTTO knew to integrate not clobber).
2. **Non-destructive git safety net** (the real guarantee): both terminals `fetch` before edit AND before
   push; **merge** if origin moved; **NEVER** force-push / rebase-push / `reset --hard` on OTTO; one
   pusher at a time.
3. **Real-time arbitration = the operator** (two AI terminals can't see each other live; git can't show
   another checkout's unpushed work). When both are live + OTTO source is in play, operator hands
   edit-ownership to one at a time.
- Plus: an **"IN-FLIGHT WORK" board** at the top of the inbox (each terminal posts its current scope at
  session start, clears on push) — advisory, not a lock.
- Plus IDA's add: **when both terminals are live, IDA REQUESTS OTTO-core changes via the inbox** instead of
  editing directly (reserve IDA's edit-autonomy for when OTTO's terminal is idle — that autonomy is what put
  two cooks in PluginProcessor.cpp).

**Division of writing (to avoid colliding on the protocol itself):**
- OTTO's terminal: adds the IN-FLIGHT board to `CROSS_PROJECT_INBOX.md` + the convention to OTTO's `CLAUDE.md`,
  AND prunes the informational `[FROM OTTO → IDA]` entry (already acted-on).
- **IDA's TODO (next session):** add the matching three-layer protocol section to **IDA's `CLAUDE.md`**
  cross-project section. NOT done yet. See todo.md.

---

## ▶ 6. Inbox state (OTTO `external/OTTO/CROSS_PROJECT_INBOX.md` @ 0e61a69f)

- All 6 `[FROM IDA → OTTO]` entries were **acked by OTTO** in the merge (EventBus left deferred per operator;
  the rest kept/not-reverted incl. the new activeBusMask "do NOT revert").
- `[FROM OTTO → IDA]`: one **informational** entry (2026-05-29, OTTO TAPECOLOR engagement/quality changes) —
  acted-on (IDA bumped the pin). OTTO's terminal is pruning it. No IDA action outstanding.
- OTTO origin/main == IDA's pin.

---

## ▶ 7. S6 status (the milestone thread) — T4–T7 done, T8 partial

- **T4–T7:** landed + reviewed + pushed. DEST picker operator-verified WORKING (after `84a9049`).
- **T8 (operator eyes-on):** the DEST picker + routing is confirmed; the **save→quit→reload round-trip was
  NOT completed** (session derailed by crackle). When resuming: have the operator run the T-S6 checklist
  (it was in the prior handoff / plan `docs/superpowers/plans/2026-05-29-otto-strip-dest-and-persistence.md`
  Task 8) — add OTTO out, route to a hardware pair, save, quit, relaunch, reopen, confirm the route + label
  survive. THEN S6 closes.
- **INS-button parity:** deferred to its own slice (operator chose "do it right"). Full write-up in todo.md
  (output-channel inserts aren't engine-processed at all — host bound to buses only; a real engine slice).

---

## ▶ 8. Next-session resume protocol

1. **Read this file.** Inbox check: no IDA action outstanding (OTTO pruning the informational entry).
   OTTO pin `0e61a69f` == origin/main.
2. **PRIORITY: the TAPECOLOR 1× overload (§4).** Try Eco-tier (quality=0) on the consumed bus first.
   Coordinate any lsfx_tapecolor/OTTO DSP change via the inbox + operator (two-terminal protocol §5).
   This is gated on the operator being able to enable TAPECOLOR without crackle.
3. **Then close S6 T8** (operator save/load round-trip) — §7.
4. **Codify the coordination protocol** in IDA's `CLAUDE.md` (§5, IDA's TODO) — quick.
5. **INS slice** (todo.md) when the operator wants it.
6. Session end — refresh this file + todo.md.

---

## ▶ 9. Known issues / non-blocking

- Pre-existing warning `MainComponent.cpp:~6051` `juce::int64 → double` (line shifts each session). Not new.
- Phrase/MON/bus output-strip solo buttons + INS buttons are visible-but-inert (deferred — todo.md).
- OTTO standalone CMake pre-existingly broken (separate). LSP fires false "file not found / undeclared"
  diagnostics on OTTO submodule edits (no JUCE include paths in that view) — confirmed noise via clean build.

---

*End of session. S6 DEST picker works (T4–T7 + the refresh fix); T8 round-trip pending. The live blocker
is that a SINGLE per-player TAPECOLOR (Standard 2× oversampled) overloads the CPU at a 512 buffer — next
session try Eco tier (quality=0, no oversampling) FIRST, coordinate any TAPECOLOR DSP change with OTTO's
terminal per the new two-terminal protocol. Three crackle fixes (denormal, MIDI-alloc, selective-bus) +
the OTTO merge all shipped + pushed; ctest 819/1; OTTO pin 0e61a69f.*
