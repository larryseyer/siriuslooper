# Session Continuation — NEXT: operator eyes-on slice U + slice P save/load round-trip

> **For a fresh chat picking this up cold:** memory + project +
> user CLAUDE.md load automatically. This file is the **forward-looking
> handoff** — only what's next matters.

## ▶ DO THIS FIRST

1. Read `external/OTTO/CROSS_PROJECT_INBOX.md`. As of session start
   2026-05-24, all entries through TAPECOLOR Phase 5 + the "OTTO pin
   bumped to c4a8ec3" informational notice are acked. Next expected
   OTTO event is TAPECOLOR Phase 6 (tape-hiss noise floor).
2. **Operator GUI eyes-on for slice U** (landed previous session):
   launch `~/Desktop/IDA` → Input Mixer tab → click a strip → the
   tabbed detail panel should show four tabs (Pan/Wid, Sends, EQ,
   CMP). Pan/Wid unchanged. Sends shows the operator's FX-return
   buses (or "No FX returns on this mixer — add one from the
   blank-area menu" if none) plus a PRE FADER toggle. EQ + CMP are
   explicit placeholder cards saying wiring lands with the insert-
   chain UI (P7). Repeat on the Output Mixer tab with a phrase strip.
3. **Operator save/load eyes-on for slice P** (landed this session):
   - With a session having at least one phrase strip on the Output
     Mixer (Mark Out a capture to create one if needed), set a
     custom fader / mute / send level / destination picker choice on
     a phrase strip.
   - File → Save As → write a new `.ida.json`.
   - Quit + relaunch IDA. File → Load → pick that file.
   - The phrase strip should reappear in the same pill-order slot
     with the same fader / mute / send / destination state. The
     ConstituentId → OutputChannelId binding now survives save/load.
   - Also check that aux buses + FX returns on either mixer survive
     (input + output mixer graph states are now persisted alongside
     the phrase-channel map).
   - Loading an older v1 envelope (`ida_version: 1`) still works —
     the mixer/map keys are optional and default to "no change to
     current state" (current pre-v2 behavior preserved).

## ▶ DONE LAST SESSION

**Slice P — session persistence for mixer graphs + phrase-channel map.**

Spec: `docs/superpowers/specs/2026-05-23-mixer-symmetry-fx-returns-sends-design.md`
§Slice P, with the slice-5c map cross-ref from
`2026-05-23-output-mixer-phrase-channels-design.md`. Continue.md's prior
note that slice P was "single-concern" reflected that `preFaderSends` +
`mainOutKind` already round-tripped through E2+E3 — but the OutputMixer
and InputMixer graph states themselves were never wired into the save
envelope (only unit-tested in isolation), so persisting the phrase-
channel map alone would have been useless (channels reset on load,
binding goes stale). Scope expanded to deliver the slice end-to-end
per `feedback_sirius_done_right_and_complete`.

### Envelope schema v2

`app/MainComponent.cpp`: `kSessionEnvelopeVersion` bumped 1 → 2.
Three new keys, all optional (v1 envelopes load clean — defaults
preserve current pre-v2 behavior):
- `input_mixer`  — serialized `InputMixerGraphState`
- `output_mixer` — serialized `OutputMixerGraphState`
- `phrase_channel_map` — list of `(constituent_id, output_channel_id)` pairs

### New persistence helpers

`persistence/include/ida/SessionFormat.h` + `persistence/src/SessionFormat.cpp`:
- `serializePhraseChannelMap (const std::vector<std::pair<int64_t, int64_t>>&) -> juce::String`
- `deserializePhraseChannelMap (const juce::String&) -> std::vector<std::pair<int64_t, int64_t>>`
  - Mirrors the TapePool convention: a missing `entries` key is a hard
    error (back-compat callers detect envelope-level absence and skip).

### OutputMixer id-preservation fix (latent bug surfaced by slice P)

`engine/src/OutputMixer.cpp` + `engine/include/ida/OutputMixer.h`:
- `OutputMixer::importGraphState` used `addChannel()`, which mints
  fresh sequential ids. For exports with gaps from `removeChannel`
  (e.g. saved channels `{1, 3, 5}`), the loaded mixer reproduced
  `{1, 2, 3}` — silently breaking any external map keyed by
  `OutputChannelId`.
- Extracted the parallel-vector wiring into a private
  `registerChannelWithId(SignalType, OutputChannelId)` helper.
  `addChannel` allocates (free-list or counter) then delegates;
  `importGraphState` calls it with the persisted `c.channelId`.
- Mirror of the pattern `InputMixer::importGraphState` already used
  for `ChannelId` preservation.

### Load-path wiring

`MainComponent.cpp` load callback:
- Parses `input_mixer` / `output_mixer` / `phrase_channel_map` from
  the envelope into `std::optional<...>` (empty on v1 envelopes).
- Inside the existing tape-pool audio-callback bracket: if a mixer
  state is present, allocate a fresh `InputMixer` / `OutputMixer`
  instance (the `importGraphState` precondition asserts no bus
  collisions), re-bind notification bus / tape sink / effect-chain
  host / tape-pool mirror, call `importGraphState`, then re-bind the
  `AudioCallback`'s mixer pointers (the only other holder — panes
  don't store mixer pointers; verified via grep across `app/` + `ui/`).
- Before the refresh cascade: clear + populate
  `phraseChannelByConstituent_` from the loaded map. The cascade
  then sees the binding already populated and skips minting fresh
  channels for surviving Constituents.

### Tests (4 new, 674/675 passing baseline — was 670/671)

`tests/SessionFormatTests.cpp` (`[phrase-channel-map]` tag):
- Round-trip with three entries; byte-stable re-serialize.
- Empty list round-trips.
- Malformed JSON throws (`{not json}`, `[1,2,3]`, missing `entries`).

`tests/OutputMixerTests.cpp` (`[output-mixer][persistence][slice-p]`):
- Create channels 1,2,3 → remove 2 → reuse → remove again →
  export. Verify the export carries ids `{1, 3}` (gap at 2). Import
  into fresh mixer; re-export carries `{1, 3}`, not `{1, 2}`.

## ▶ BASELINE (start of next session)

- `ctest --test-dir build`: **674 pass / 1 not-run / 675 total**.
  The not-run is the `MainComponentPluginEditorTests_NOT_BUILT`
  sentinel (unchanged from prior baseline).
- Build clean: `cmake --build build --target IDA IdaTests -j`
  exits 0. `~/Desktop/IDA` alias points at
  `build/app/IDA_artefacts/Release/IDA.app`.
- `master` HEAD on origin: will be bumped this commit (slice P land).
- OTTO submodule SHA: `3c84a409` (unchanged).
- lsfx_tapecolor submodule SHA: `d8b06b1` (unchanged).

## ▶ NEXT

Mixer-symmetry design is fully landed (E1 → E2 → E3 → U → P).
Engine roadmap (per `project_mixer_then_transport_roadmap`) returns
to operator's next priority — **transport + metering surfaces** (the
"and then discuss transport + other metering" half of the roadmap).
Open with a brainstorming pass on what transport surfaces look like
in IDA — the white paper Part IX is the design reference; OTTO's
transport bar is the visual reference.

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
   IdaTests IDA -j`. Slice P touched the load path significantly;
  consider a clean rebuild before the save/load eyes-on if anything
  feels off.
- **Pre-v2 envelope back-compat lives on grace not testing.** v1
  envelope files (saved before this session) deserialize via the
  same code path: the new envelope keys are optional and absent v1
  files load with the mixers/map at their pre-load state (current
  behavior). There's no test fixture for a v1 envelope going through
  the loader yet — if back-compat regresses it'll surface as
  operator-visible "old session loaded but mixer routing is wrong";
  worth adding a fixture-based test if/when that happens.
- **OTTO ChannelDetail wrapper still not vendored** — if a future
  slice needs the OTTO preset bar or OTTO's full EQ/CMP panels in
  IDA, that's a "drag in OTTO's PresetManager + panels" decision
  the operator deferred a session ago.
