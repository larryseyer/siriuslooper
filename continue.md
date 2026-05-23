# Session Continuation — NEXT: Output Mixer tab (T4 prereq) or T6 persistence

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. Ack any
   `[FROM OTTO → IDA]` entries (set `Status: acked YYYY-MM-DD`, add a
   `Resolution:` line, then act on the guidance). Last check: that
   section was empty; the only needs-ack entry is `[FROM IDA → OTTO]`
   (the IDA rename announcement), which is addressed to OTTO's Claude
   — not ours to ack.

## ▶ STATUS — P7 T5 COMPLETE (slices 1-5 all landed)

Operator confirmed at 2026-05-23 session close: **INS button visible on
every input + bus strip; click brings up the InsertChainPopup with 8
empty slots.** Visual surface is good. The deeper audible verification
(drop RVB → hear plate reverb tail, toggle bypass mid-signal, reorder
EQ over RVB, save+reopen) was NOT exercised this session — operator
moved on. If anything misbehaves when they do test, the most likely
suspects are:

- `growChainToSlot` caps at `kMaxSlots-1 = 7`; picking slot 8+ no-ops.
- INS row eats ~58 px of bottom band — tight on short windows.
- CallOutBox anchors to the INS button's screen rect; off-screen
  button → upper-left fallback.

T5 implementation summary (one commit, `5db3541`):
- `InputMixerPane` per-strip INS buttons on both channel + bus rows,
  stacked above the destination picker (T5-plan risk #2 mitigation).
- `MainComponent::openInsertChainPopupForChannel` /
  `openInsertChainPopupForBus` translate the popup's four callbacks
  into a uniform `removeAudioCallback → setEffectChain(updated) →
  addAudioCallback` cycle. Slice 3's chain-set sweep re-binds every
  Internal slot AND re-asserts bypass per slot, so one engine call
  covers add/remove/reorder/bypass — no parallel per-slot path.
- Popup launched via `juce::CallOutBox::launchAsynchronously` anchored
  to the INS button's screen rect.

## ▶ NEXT — operator's mixer→transport roadmap continues

Memory: `project_mixer_then_transport_roadmap` — Input Mixer →
Output Mixer → transport. The choices for the next session are:

### Option A — start the Output Mixer tab

Mandatory prereq for T4 Sends UI per
`project_two_mixers_totally_separate`. Whitepaper §5.2/§6.6/§7.1: the
Output Mixer is the mixdown console (one channel per phrase → stereo
outputs/buses/master), totally separate state from the Input Mixer.
Reuse generic `MixerGraph` / `Bus` types per-instance, never a shared
core. `OutputMixer` already exists in the engine; this would be the
GUI tab + per-strip wiring (mirroring InputMixerPane's shape but with
phrase channels instead of physical-input channels).

### Option B — T6 P4/P5 persistence wiring

The EffectChain already round-trips through `SessionFormat` (slice 3
made `setEffectChain` propagate bypass; persistence already
serializes both `internalId` and `bypassed`). T6 would verify the
save/load path end-to-end with the new insert UI — likely a session
serializing a Bus with an RVB+bypass insert, loading on the other
side, and confirming the popup re-seeds correctly. Smaller scope than
A.

### Option C — audible gate cleanup on T5

If the operator does test the audible path next session and something
misbehaves, fixing that takes priority over A/B.

**Recommendation:** start A. T4 (Sends UI) is blocked on the Output
Mixer tab, and B is verifiable in <100 lines once A lands.

## ▶ BASELINE

- `ctest --test-dir build`: **639 pass / 1 not-run / 640 total**. The
  not-run is the operator-only `MainComponentPluginEditorTests_NOT_BUILT`
  sentinel — unchanged from baseline.
- `master` HEAD on origin: `5db3541` (P7 T5 slice 5).
- OTTO submodule SHA: `6b37609e` on `origin/main`.

## ▶ HOUSEKEEPING

- **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (underscores —
  `project_whitepaper_path`).
- **Operator actions still pending** (between sessions; agent cannot
  perform; tracked in `todo.md`): notarytool keychain `ida-notary`
  setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
- **Clean build before any GUI smoke** (`feedback_clean_builds`):
  `rm -rf build && cmake -B build -S . -G Ninja
   -DCMAKE_BUILD_TYPE=Release && cmake --build build --target
   IdaTests IDA -j`.
