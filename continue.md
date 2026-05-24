# Session Continuation — NEXT: operator eyes-on slice U + slice P (5c map persistence)

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session start
   2026-05-24, all entries through TAPECOLOR Phase 5 + the "OTTO pin
   bumped to c4a8ec3" informational notice are acked. Next expected
   OTTO event is TAPECOLOR Phase 6 (tape-hiss noise floor).
2. **Operator GUI eyes-on for slice U** (this session's land): launch
   `~/Desktop/IDA` (the alias) → Input Mixer tab → click a strip →
   the tabbed detail panel should show four tabs (Pan/Wid, Sends, EQ,
   CMP). Pan/Wid is unchanged. Sends shows the operator's FX-return
   buses (or "No FX returns on this mixer — add one from the blank-area
   menu" if none) plus a PRE FADER toggle. EQ + CMP are explicit
   placeholder cards saying wiring lands with the insert-chain UI (P7).
   Repeat on the Output Mixer tab with a phrase strip.
3. Remaining design-spec slice: **slice P** (slice-5c `ConstituentId →
   OutputChannelId` map persistence). `preFaderSends` + `mainOutKind`
   already round-trip via E2 + E3.

## ▶ DONE LAST SESSION

**Slice U — Tabbed `ChannelDetail` on both mixers (IDA-native).**

The original spec called for "wire OTTO's ChannelDetail unchanged." On
inspection OTTO's wrapper requires `otto::presets::PresetManager`, OTTO's
Sends tab hardcodes `mixer::kNumFxReturns = 4` (IDA is dynamic N), and
OTTO's EQ/CMP tabs embed full OTTO panels coupled to OTTO's
PluginProcessor. Operator-approved restructure: IDA-native
`ida::ui::ChannelDetail` mirroring OTTO visually; real Sends tab; explicit
placeholder EQ/CMP cards.

### New files

- `ui/include/ida/ChannelDetail.h` + `ui/src/ChannelDetail.cpp`
  - `ida::ui::ChannelDetail` — tabbed wrapper (Pan/Wid · Sends · EQ · CMP).
  - `ida::ui::ChannelDetailSendsTab` — IDA-native Sends tab. Dynamic N
    cards (one per FX-return bus on the host mixer), per-card rotary +
    dB readout, top-of-panel PRE FADER toggle (E2 hookup). Mixer-
    agnostic — the host pane snapshots roster + send levels via
    `setChannelState`. Card border color uses
    `ida::palette::hueForId(busId)` (sister-app shared palette).
  - `ida::ui::ChannelDetailPlaceholderTab` — paints "wiring lands with
    the insert-chain UI (P7)". Used for EQ + CMP per
    `feedback_sirius_done_right_and_complete` (explicit not-broken, not
    half-baked).
  - Pan/Wid tab keeps the existing vendored `otto::ui::ChannelDetailPanWidTab`
    (the only one of OTTO's four tabs without a PresetManager dep).
- `ui/CMakeLists.txt`: `src/ChannelDetail.cpp` added to `IdaUi`.

### Wired into both panes

- `MainComponent.cpp` — `InputMixerPane` + `OutputMixerPane` both:
  - Field swap: `otto::ui::ChannelDetailPanWidTab detailPanel_`
    → `ida::ui::ChannelDetail detailPanel_`.
  - New listener inheritance: `public ida::ui::ChannelDetailSendsTabListener`.
    Existing `ChannelDetailPanWidTabListener` inheritance stays; both
    listeners attach via `detailPanel_.panWidTab().addListener(this)` /
    `detailPanel_.sendsTab().addListener(this)`.
  - `showDetailFor` / `showPhraseDetailFor` gain three params
    (`fxReturns`, `sendLevels`, `preFader`) feeding the new Sends tab.
  - New pane callbacks: `onSendChanged(stripIdx, fxReturnIdx, level)` and
    `onPreFaderToggled(stripIdx, preFader)` (mirrors on output side
    are `onPhraseSendChanged` + `onPhrasePreFaderToggled`).
- `MainComponent.h` — new helpers `collectInputSendsView(stripIdx)`
  and `collectOutputSendsView(phraseIdx)` snapshot the FX-return roster
  + send levels + pre-fader flag for the named strip.
- `MainComponent.cpp` — the onSelect lambdas call the collectors and
  hand the snapshot to `showDetailFor`. Send-change lambdas re-walk the
  mixer's FxReturn-kind buses in the same order to recover the BusId
  (the snapshot index → BusId mapping is rebuilt fresh on every gesture,
  not cached, so an FX-return add/remove between show and gesture
  doesn't desync). The input pane calls `InputMixer::setChannelSend` /
  `setChannelSendIsPreFader`; the output pane calls
  `OutputMixer::routeChannelToBus` (additive on FxReturn-kind per E3) /
  `setChannelSendIsPreFader`.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **670 pass / 1 not-run / 671 total**
  (unchanged from session-start baseline; the not-run is the
  `MainComponentPluginEditorTests_NOT_BUILT` sentinel — unchanged).
  Slice U is UI-only; no engine tests added.
- Clean rebuild verified — `~/Desktop/IDA` alias points at
  `build/app/IDA_artefacts/Release/IDA.app`.
- `master` HEAD on origin: will be bumped this commit (slice U land).
- OTTO submodule SHA: `3c84a409` (unchanged).
- lsfx_tapecolor submodule SHA: `d8b06b1` (unchanged, Phase 1+2+3+4+5).

## ▶ NEXT — slice P (the one remaining bit)

Spec sections P in
`docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`.

### Slice P — Slice-5c (`ConstituentId → OutputChannelId`) persistence

- The phrase-channel map (`MainComponent::phraseChannelByConstituent_`)
  is rebuilt from constituent ids on session load. Persist it so phrase
  strips don't reshuffle across reloads. Reference:
  `docs/superpowers/specs/2026-05-23-output-mixer-phrase-channels-design.md`
  (slice 5c).
- The other two fields the spec listed for slice P (`preFaderSends` +
  `mainOutKind`) already round-trip end-to-end (E2 + E3). Slice P is
  now a single-concern slice.

### After slice P

Mixer-symmetry design is fully landed. Engine roadmap (per
`project_mixer_then_transport_roadmap`) returns to operator's next
priority — transport + metering surfaces.

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
   IdaTests IDA -j`.
- **OTTO ChannelDetail wrapper not vendored** — if a future slice
  needs the OTTO preset bar or OTTO's full EQ/CMP panels in IDA,
  that's a "drag in OTTO's PresetManager + panels" decision the
  operator deferred this session. Reopen with a fresh design pass.
