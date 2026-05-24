# Session Continuation — NEXT: Slice EC (EQ + CMP tabs functional, both mixers)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session start
   2026-05-24, all entries through TAPECOLOR Phase 5 + the "OTTO pin
   bumped to c4a8ec3" informational notice are acked. No new OTTO
   events landed in the session that produced this handoff. Next
   expected OTTO event is TAPECOLOR Phase 6 (tape-hiss noise floor).
2. **Confirm the parity invariant before writing any code** (section
   below). Slice EC is fundamentally a parity-realization slice —
   the EQ + CMP tabs ship identically on both mixers, instanced
   twice. If you find yourself adding mixer-specific logic, stop.
3. Begin slice EC per the plan below. Start with the brainstorming
   skill if any concrete sub-decision below feels under-specified —
   the operator has stated they want it done in this session.

## ▶ THE PARITY INVARIANT (load-bearing — restated by operator 2026-05-24)

> "input mixer and output mixer are IDENTICAL except for inputs and
> outputs... very important!"
>
> "What I mean is their channels are identical... of course, the
> number of channels for each will be different"
>
> "input mixer goes to tapes (or other) and output mixer comes from
> phrases and goes to outputs etc..."

What this means in code:

- The **channel/strip UI is identical between InputMixerPane and
  OutputMixerPane**: fader, mute, INS, pan/width, **EQ**, **CMP**,
  sends, destination picker. Same UI types per-instance (memory:
  `project_two_mixers_totally_separate` — types shared, never
  state).
- The two consoles differ **only** at the I/O bookends:
  - Input: physical input → strip → (tapes | aux buses |
    hardware-output | FX-return sends).
  - Output: phrase render (or bundled OTTO output, later) → strip
    → (master | aux buses | hardware-output | FX-return sends).
- Channel **count** is operator-driven on both, independently.
- Slice U gave both panes the same `ida::ui::ChannelDetail`
  instance type — slice EC continues that pattern with the new EQ
  and CMP tab components.

## ▶ DONE LAST SESSION

This session shipped two pieces and surfaced one big design gap
(slice EC, queued for the next session).

### 1. Slice P — session persistence for mixer graphs + phrase-channel map (HEAD `f1e0fb0`)

Envelope schema v2 (bumped `kSessionEnvelopeVersion` 1 → 2). Three
new optional keys: `input_mixer`, `output_mixer`,
`phrase_channel_map`. v1 envelopes still load clean (keys are
optional; absence preserves pre-v2 behavior).

- New persistence helpers in
  `persistence/include/ida/SessionFormat.h` +
  `persistence/src/SessionFormat.cpp`:
  `serializePhraseChannelMap` + `deserializePhraseChannelMap`.
- Latent bug fix: `OutputMixer::importGraphState` used
  `addChannel` (which mints fresh ids), silently breaking exports
  with `removeChannel`-induced id gaps. Extracted
  `registerChannelWithId` helper; import now preserves persisted
  ids (mirror of InputMixer's pattern at
  `engine/src/InputMixer.cpp:367-383`).
- `MainComponent` save/load now wires both mixer graphs + the map
  through the envelope; load replaces mixer instances with fresh
  ones, re-binds notification bus / tape sink / effect-chain host
  / tape-pool mirror, calls importGraphState, re-points the
  AudioCallback's mixer pointers, then restores the phrase-channel
  map **before** the refresh cascade so surviving constituents
  re-bind to their saved OutputChannelIds.
- 4 new tests (3 `[phrase-channel-map]` + 1
  `[output-mixer][persistence][slice-p]`). Baseline 674 pass / 1
  not-run / 675 total. Not-run is the unchanged
  `MainComponentPluginEditorTests_NOT_BUILT` sentinel.

**Operator eyes-on still pending for slice P** (queued — see
"Also Pending" below). Save → quit → relaunch → load: phrase strip
mix state should survive.

### 2. Bug fix — Output Mixer "Add FX return" UI surface (HEAD `a8e0551`)

Discovered while the operator was testing slice U:
**InputMixerPane's blank-area menu had "Add bus", "Add FX return",
and "Add tape" items, but OutputMixerPane only had "Add bus".**
E1 added `BusKind::FxReturn` to OutputMixer (engine surface) but
the UI gesture to actually create one was never wired. Without an
FX-return bus on the Output Mixer, the Sends tab on phrase strips
showed "No FX returns on this mixer" forever — manifesting as the
operator's "I don't see anything in the tabs" complaint.

Fix in `app/MainComponent.cpp`:
- Added `onAddFxReturn` callback to OutputMixerPane (mirror of
  InputMixerPane's).
- Added "Add FX return" item to OutputMixerPane's
  `showBlankAreaMenu`.
- Wired in MainComponent ctor:
  `outputMixer_->addBus(BusConfig{ 2, "FX N", BusKind::FxReturn })`,
  bracketed by audio callback removal, refresh after.

This restores parity for the blank-area menu — the operator can
now create FX returns on both mixers. Sends tab populates as
expected on both.

### 3. Surfaced (NOT shipped) — Slice EC scope conversation

Operator clicked the EQ + CMP tabs and reported "no EQ or CMP"
(meaning: they see the tab buttons but want functional UI, not the
italic-placeholder cards slice U shipped). Operator restated the
parity invariant (section above) and authorized building real EQ +
CMP UI in this session. The remainder of this file is the slice EC
plan.

## ▶ NEXT — Slice EC: EQ + CMP tabs functional on both mixers

### What exists today (verified this session — DO NOT re-discover)

- **Internal FX enum:** `core/include/ida/InternalFxId.h` —
  `kEq`, `kCmp`, `kRvb`, `kDly`.
- **EQ adapter:** `engine/src/fx/EqAdapter.{h,cpp}` —
  IDA-side wrapper around OTTO's header-only
  `otto::effects::PlayerEQ`. Holds a `PlayerEQ` + a
  `PlayerEffectsConfig` value member. Today's constructor flips
  `cfg_.eqEnabled = true` and ships the default-flat curve.
  Header comment explicitly says: *"T3a ships default config /
  no operator parameter UI — a later UI slice exposes the
  parameter surface."* Slice EC IS that deferred UI slice.
- **CMP adapter:** `engine/src/fx/CmpAdapter.{h,cpp}` — symmetric
  pattern around `otto::effects::PlayerCompressor`.
- **Factory + dispatch:** `engine/src/InternalFxFactory.cpp` —
  given an `InternalFxId`, returns the matching
  `std::unique_ptr<IInternalFxAdapter>`.
- **Interface:** `engine/include/ida/IInternalFxAdapter.h` —
  prepare / reset / process. Today there are NO setters for the
  PlayerEffectsConfig fields; slice EC needs to add them (or
  expose the config reference through the interface).
- **Parameter surface (from OTTO's `PlayerEffectsConfig`):**
  - EQ: `eqEnabled`; HP `eqHPFreq` (20-500 Hz) + `eqHPSlope`;
    Low shelf `eqLowGain` (-12..+12 dB) / `eqLowFreq` (40-500
    Hz) / `eqLowQ` (0.1-10); Mid parametric `eqMidGain` /
    `eqMidFreq` (200-8000 Hz) / `eqMidQ`; High shelf
    `eqHighGain` / `eqHighFreq` (2000-16000 Hz) / `eqHighQ`;
    LP `eqLPFreq` (2000-20000 Hz) + `eqLPSlope`.
  - CMP: `compEnabled`; `compThreshold` (-60..0 dB);
    `compRatio` (1..20); `compAttack` (0.1..100 ms);
    `compRelease` (10..1000 ms); `compMakeup` (0..24 dB);
    `compMix` (0..1, parallel-comp); `compSidechainHPF` bool.
  - Read directly from
    `external/OTTO/src/otto-core/include/otto/effects/PlayerEffects.h:134-249`
    if you need the exact defaults or any field I left out.
- **OTTO's panels** (visual reference only — DO NOT vendor as-is):
  `external/OTTO/src/otto-plugin/ui/panels/EQPanel.{h,cpp}`
  (476 + 1420 lines) and `CompressorPanel.{h,cpp}` (338 + 789
  lines). Both depend on `otto::presets::PresetManager`,
  `SpectrumDisplay`, `PresetTypes`, and `EQBindingAdapter` —
  vendoring drags in OTTO subsystems IDA doesn't have. Same
  blocker that made slice U punt. Use them as visual reference
  (colors, layout, control idioms) — implement IDA-native.
- **The slice U framework:** `ui/include/ida/ChannelDetail.h` +
  `ui/src/ChannelDetail.cpp`. Today `ChannelDetail` constructs
  four tabs: `panWidTab_` (vendored OTTO), `sendsTab_` (IDA-
  native real, slice U), `eqTab_` + `cmpTab_` (both currently
  `ChannelDetailPlaceholderTab` instances). Slice EC replaces
  `eqTab_` + `cmpTab_` with new IDA-native tab components.

### What needs to be built — slice EC subtasks

Order them; subtask 1 unblocks 2, 2 unblocks 3-5, etc.

#### EC1 — Adapter parameter surface

`engine/include/ida/IInternalFxAdapter.h`: add either
(a) message-thread-only `setConfig(const PlayerEffectsConfig&)` +
`config() const` accessors on each adapter, OR
(b) a generic typed parameter set/get interface. **Recommendation:**
(a) is simpler — just add the two methods on `EqAdapter` and
`CmpAdapter`, no interface change. The audio thread reads via a
double-buffered swap (per the CmpAdapter header comment referring
to OTTO's MasterBus config-swap pattern); the message thread
writes via `setConfig`. Initial slice EC can use a plain mutex-
free atomic-swap with `std::atomic<PlayerEffectsConfig*>` since
both adapters today are value-members, not heap-owned — needs a
small design call. **Default to the config-swap pattern** OTTO's
MasterBus uses (referenced in `CmpAdapter.h` comment header) —
`scratchConfig()` / `commitConfig()` / `liveConfig()`.

Add Catch2 coverage:
`tests/EqAdapterTests.cpp` + `tests/CmpAdapterTests.cpp`
(check whether these exist; if so extend, if not create) —
setConfig → process produces audibly different output (e.g.
a sine through a -12 dB low shelf attenuates predictably; a
compressor with threshold -∞ ratio ∞ flattens to threshold).
Cover the audio-thread contract via the same grep the EqAdapter
header documents.

#### EC2 — Adapter binding helper (host side)

Each channel strip needs a discoverable EQ adapter + CMP
adapter instance. Two design choices on the table:

**(α) Strip-owned slots.** Channel strip auto-inserts an EQ
slot and a CMP slot in its EffectChain at construction time, at
fixed indices (e.g. 0 = EQ, 1 = CMP per
`project_internal_fx_first_class`'s "EQ → CMP → DLY → RVB"
sequence). Adapters live in the engine's dispatch path. UI
finds them by walking the chain for `EffectChainEntry::makeInternal(kEq)`
/ `kCmp`.

**(β) Side-channel adapter store.** A separate per-channel
`std::unordered_map<ChannelId, EqAdapter*>` in MainComponent
(or in the mixer). UI looks up by ChannelId. EffectChain stays
operator-driven (slot is only present if the operator added
one via INS).

**Recommend (α)** because:
- The parity invariant requires EQ + CMP "always present" on
  every channel; (β) makes them feel optional.
- Matches OTTO's signal-flow comment: "Synth → EQ →
  Compressor → IR → Delay → Output" — fixed sequence per
  channel.
- `project_internal_fx_first_class` memory says the same:
  "EQ→CMP→DLY→RVB sequence".

Open question for (α): do EQ + CMP slots show up in the INS
popup too? Probably yes — operator can bypass/reorder/remove
them via INS, with `setEffectChain` re-asserting the slots if
they get removed (or just allow removal — the EQ/CMP tab UI
then shows a "no slot present" empty state instead of forcing
re-insertion). **Default:** allow operator to remove; EQ/CMP
tabs gracefully show an "add" prompt when the slot is absent.
If operator finds this annoying, flip to "always reserved".

#### EC3 — `ida::ui::ChannelDetailEQTab`

`ui/include/ida/ChannelDetailEQTab.h` + `ui/src/ChannelDetailEQTab.cpp`.

Shape mirrors `ChannelDetailSendsTab` from slice U:
- `struct ChannelState { PlayerEffectsConfig config; bool hasEqSlot; }`.
- `setChannelState (const ChannelState&)` — host pane pushes
  when selection or chain changes.
- `class ChannelDetailEQTabListener` with one method
  `eqTabConfigChanged (const PlayerEffectsConfig&)` (fired on
  any control change).
- Layout for v1: enable toggle + 12 sliders + 12 numeric
  readouts in a grid (HP freq + 4 shelves × 3 controls + LP
  freq, slope toggles inline). No curve viz, no spectrum
  underlay (queued — see "Out of scope" below).
- Visual style: read OTTO's `EQPanel.cpp` for colors, label
  fonts, slider styles. Use OTTOLookAndFeel's existing token
  accessors per the GRIM Menu Surface rule (no hardcoded
  font/color values).

#### EC4 — `ida::ui::ChannelDetailCMPTab`

Symmetric to EC3. 7 controls (threshold, ratio, attack, release,
makeup, mix, sidechain-HPF toggle) + enable toggle. Visual
reference: `external/OTTO/src/otto-plugin/ui/panels/CompressorPanel.cpp`.
No gain-reduction meter for v1 (queued — needs adapter to
expose live gain-reduction value, separate work).

#### EC5 — Swap into `ChannelDetail`

`ui/src/ChannelDetail.cpp` lines 264-272 currently construct two
`ChannelDetailPlaceholderTab` instances. Replace with the new
`ChannelDetailEQTab` and `ChannelDetailCMPTab`. Update
`ChannelDetail`'s public accessors (`eqTab()`, `cmpTab()`)
analogous to the existing `panWidTab()` / `sendsTab()`.

#### EC6 — MainComponent wiring

`app/MainComponent.cpp` — both panes:
- `InputMixerPane`: inherit `ChannelDetailEQTabListener` +
  `ChannelDetailCMPTabListener` alongside the existing
  `Sends`/`PanWid` listeners. Wire via
  `detailPanel_.eqTab().addListener (this)` /
  `detailPanel_.cmpTab().addListener (this)`.
- Extend `showDetailFor` to accept the strip's EQ/CMP config
  (read from the strip's adapter via the EC2 helper) and push
  into the tabs via their `setChannelState`.
- On `eqTabConfigChanged` / `cmpTabConfigChanged`: look up the
  strip's adapter and call `setConfig`. Wrap in the existing
  audio-callback bracket pattern.
- `OutputMixerPane`: same wiring, against phrase-strip
  adapters (look up via
  `phraseChannelByConstituent_[cid.value()]` → OutputChannelId
  → adapter pointer).

#### EC7 — Persistence (THIS slice or follow-up?)

The PlayerEffectsConfig values need to round-trip through
save/load. Two paths:

**Inline in EffectChainEntry.** Add a `PlayerEffectsConfig`
optional field to `EffectChainEntry` (or a generic JSON-shaped
"state" payload). Serialize via existing chain persistence.
Loud at load time when the slot is reconstructed: re-apply the
config via `adapter->setConfig`.

**Separate per-channel config sidecar.** Persist a
`{ ChannelId : PlayerEffectsConfig }` map alongside the mixer
graph state in the envelope (similar to slice P's
phrase-channel map).

**Recommend inline** — config is per-slot, not per-channel; if
the operator reorders slots, the config moves with the slot.
But: requires `EffectChainEntry` schema bump, and the persistence
team (= you, next session) has to land this carefully.

**Scope call:** v1 of slice EC ships without persistence —
EQ/CMP defaults on every fresh strip, gestures land at runtime
but don't save. v2 (next-next session) adds persistence. The
operator can hot-iterate UI without waiting on the persistence
work, and the operator's reaction to v1's UI shape will inform
whether config-as-slot-payload vs sidecar wins.

### Scope discipline — what's IN this slice vs queued

**IN slice EC (must ship together for parity):**
- EC1 + EC2 + EC3 + EC4 + EC5 + EC6.
- Both mixers, identical components.
- Sliders + numeric readouts + enable toggle + slope toggles
  for HP/LP. Plain layout (no curve viz).
- Catch2 coverage for EC1 (adapter parameter setters).

**QUEUED (do not pull into slice EC):**
- EC7 (persistence) — explicit follow-up slice.
- Curve display + spectrum underlay (the OTTO EQPanel's
  signature visual — needs a separate spectrum-source
  abstraction).
- Per-band bypass (EQ) and gain-reduction meter (CMP).
- Preset save/load (depends on a preset system IDA doesn't have).
- DLY + RVB tabs (analogous slices EC-DLY + EC-RVB later).
- Plugin scanner / VST-in-INS unblock — that's "P7-scanner"
  in the prior queue, orthogonal to slice EC.

### Open design questions to settle BEFORE coding

1. **Config-swap pattern** for adapter setters — exact shape
   (use OTTO's `MasterBus.h:217-240` as reference, mirror that).
2. **Always-present slots vs operator-removable** — default
   recommended above is "operator-removable, tab shows empty
   state when absent". Confirm with operator if uncertain.
3. **EffectChain insertion order** — slot 0 = EQ, slot 1 = CMP
   in every strip's chain. Confirms the OTTO signal-flow
   convention.

Resolve in a brief brainstorming pass at the start of the next
session, then proceed to code.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **674 pass / 1 not-run / 675 total**.
  Slice EC adds at least 2 test cases (EQ + CMP adapter parameter
  round-trip) — target 676+ pass after EC1 lands.
- Build clean as of session end: `cmake --build build --target
  IDA IdaTests -j` exits 0. `~/Desktop/IDA` alias points at
  `build/app/IDA_artefacts/Release/IDA.app`. Rebuild from
  scratch (`rm -rf build && ...`) recommended before the slice EC
  operator eyes-on, per `feedback_clean_builds`.
- `master` HEAD on origin: `a8e0551` (Output FX-return UI fix on
  top of `f1e0fb0` slice P).
- OTTO submodule SHA: `3c84a409` (unchanged).
- lsfx_tapecolor submodule SHA: `d8b06b1` (unchanged, Phase 5).

## ▶ ALSO PENDING — slice P operator eyes-on (carried forward)

Operator did NOT exercise save/load this session — they were on
the slice U surfaces (and surfaced the Add-FX-return and EQ/CMP
gaps). Slice P shipped + the persistence helpers are tested in
isolation, but the end-to-end save → quit → relaunch → load
flow has not been operator-verified.

When you have a moment between EC subtasks (or after EC v1
lands), ask the operator to:
1. With a session that has at least one phrase strip on the
   Output Mixer (Mark Out to create one if needed), set a
   custom fader / mute / send level / destination picker
   choice on a phrase strip.
2. File → Save As → write a new `.ida.json`.
3. Quit + relaunch IDA. File → Load → pick that file.
4. Verify the phrase strip reappears with the same fader / mute
   / send / destination state.
5. Verify aux buses + FX returns on either mixer survive too.

Loading a pre-slice-P (v1) envelope file should still work —
mixer state defaults to "no change to current state", phrase
strips re-mint fresh (current pre-v2 behavior).

## ▶ HOUSEKEEPING

- **Whitepaper:** `docs/IDA_Whitepaper_V8.md` (underscores —
  `project_whitepaper_path`).
- **Operator actions still pending** (between sessions; agent
  cannot perform; tracked in `todo.md`): notarytool keychain
  `ida-notary` setup; `automagicart.com/ida` product page +
  `larryseyer.com` rename.
- **Clean build before any GUI smoke** (`feedback_clean_builds`):
  `rm -rf build && cmake -B build -S . -G Ninja
   -DCMAKE_BUILD_TYPE=Release && cmake --build build --target
   IdaTests IDA -j`. Slice EC touches engine + UI + MainComponent —
  do a clean rebuild before asking the operator to eyes-on.
- **OTTO panels not vendored** — slice U deferred that and slice
  EC reaffirms it (the IDA-native path is the elegant one given
  PresetManager / SpectrumDisplay coupling on OTTO's side). If a
  later slice decides to drag them in anyway, that's a fresh
  design discussion.
- **Auto-memory updates not needed this session.** No new
  durable preferences, project facts, or feedback patterns
  surfaced that weren't already captured in existing memory
  files (the parity invariant is already in
  `project_two_mixers_totally_separate`; the
  "professional and elegant" default in
  `feedback_default_to_professional_elegant`;
  "no half-baked features" in
  `feedback_sirius_done_right_and_complete`; "defer big design
  to its own session" in `feedback_defer_big_design_to_own_session`
  — slice EC follows all of these).
