# Phrase Modes (ADD/OVER), Collapse/Expand, Mode UI, Per-Phrase MIDI Trigger — Plan

> Standalone plan for four phrase capabilities, **merged into** the master roadmap
> `docs/superpowers/plans/2026-05-30-blank-slate-first-run-implementation.md` as Slices 9–12 (+ a
> post-M13 Collapse/Expand item). Reuses the existing loops-within-phrases substrate: Slice 5
> (per-phrase state machine + source-agnostic command layer), Slice 6 (one Output-Mixer channel per
> loop `T#P#L#` → per-phrase bus → master), Slice 8 (phrase-button bank + per-button MIDI storage).
> Design "why": `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` §8.

## Goal

Give phrases a per-loop **ADD/OVER** mode driven by a global top-bar toggle (and MIDI), reversible
**Collapse/Expand** of a phrase to a rendered clip, and **live MIDI triggering** of phrases on the
channel-9 control channel — without duplicating or contradicting the Slice 5/6/8 work.

## Resolved forks (operator, 2026-05-31)

1. **Mode scope = global-live, recorded per loop.** A single top-bar toggle (and MIDI) sets the
   *current* mode; each loop is born under whatever mode is active and remembers it. **A phrase may
   contain a mix of ADD and OVER loops.** Per-loop mode drives both track layout and playback resolution.
2. **Collapse/Expand sequenced after M13.** IDA has no offline render-to-file path yet (the unbuilt
   whitepaper §6.11 "render is playback aimed at a file"). Modes + toggle + MIDI land now; Collapse/Expand
   lands later as a thin consumer of the M13 File-I/O / export render path.

## The four additions

### A1 — Phrase Modes: ADD and OVER (per-loop, global current mode)
- **ADD (additive, default).** New loop layers and plays together with existing data. Layout: phrase
  keeps its own track (loop 0's channel); **each ADD loop gets its own Output-Mixer channel** `T#P#L#`
  (Slice 6 behavior).
  - **Loop fill (ADD only, global setting):** an ADD loop shorter than its covered span **loops to fill**
    (default) or **plays once**. One global toggle for all ADD loops.
- **OVER (overdub-replace).** New loop **replaces the phrase's audible content for its span**
  (loop-start..loop-end relative to phrase start) — a *playback substitution*, never a destructive edit;
  source data preserved. OVER loop **always plays once** (never fills). Layout: phrase keeps its own
  track; **each OVER loop shares the phrase's track — no separate channel.**
- **Mode recorded per loop** at creation from the global current mode; playback resolution + mixer
  routing branch on it.

### A2 — Collapse / Expand a phrase  *(deferred — after M13)*
- Per-phrase, toggleable any time. Applies to **ADD loops** (own tracks); OVER loops are already
  effectively collapsed.
- **Collapse:** render parent phrase data + ADD loops — **applying each loop's own Output-Mixer FX** —
  into **one new audio clip** that *temporarily* stands in on the phrase's track; the ADD loops'
  individual channels are removed.
- **Expand:** restore the phrase's original data and re-insert all original loop channels.
- **Non-destructive & reversible:** originals always preserved; the collapsed clip is a temporary
  stand-in, never a replacement for the source.

### A3 — UI for phrase mode (ADD / OVER)
- Global toggle **immediately to the right of the play/pause button** on the top bar. Label **ADD** /
  **OVER**. Switchable by toggle **or** MIDI note/CC (A4).

### A4 — Per-phrase MIDI trigger + live MIDI firing
- Each phrase **remembers its own MIDI trigger** (channel, note, *or* CC).
- Phrases fire two ways: the phrase **buttons** (Slice 8) **and MIDI directly**.
- **Channel 9 is the control channel** (IDA-wide). Default positional map: phrase *n* → note *n* / CC *n*.
- The A3 mode toggle is also MIDI-assignable on channel 9.
- Pulls Slice 8's deferred live-trigger (§14) forward; reuses its per-button assignment storage.

### Global settings introduced
- **ADD-mode loop fill** — loop-to-fill (default) | play-once.
- **Phrase mode** — ADD (default) | OVER — set from the top bar and/or via MIDI.

## Build-order placement (dependency-ordered)

| Slice | Depends on | Rationale |
|---|---|---|
| **9 — Phrase-mode data model + global settings** | Slice 5 | Data model before any reader. |
| **10 — ADD/OVER playback + track layout** | Slices 6, 9 | Needs per-loop channels + the mode field. |
| **11 — Top-bar ADD/OVER toggle UI** | Slices 9, 10 | UI after model + behavior. |
| **12 — Per-phrase MIDI trigger + live MIDI input** | Slices 5, 8, 11 | MIDI last: command layer + bank + toggle. |
| **(after M13) — Collapse / Expand** | Slice 10 + **M13** | ADD layout + offline render-to-file. |

## Slice 9 — Phrase-mode data model + global settings  *(headless TDD)*
- **Goal:** a loop carries a persisted ADD/OVER mode; two global settings exist and round-trip.
- **First step:** amend spec §8 (new §8.7 modes/loop-fill) before code.
- **Files:**
  - `core/include/ida/Constituent.h` — `enum class LoopPlaybackMode { Add, Over };` + an optional
    per-loop field (a loop is a Constituent with a `TapeReference`; the mode rides alongside it).
    **Distinct from `Promotion.h` `AttachmentMode { Shared, Overlay }`** (placement-sharing axis, not
    layer-vs-replace).
  - `core/include/ida/Promotion.h` — `promote()` stamps the new loop with the current mode (parameter,
    default `Add`).
  - `app/IdaPreferences.h` — `addModeLoopFill()` (bool, default `true`) + `currentPhraseMode()`
    (`Add` default) over the existing `prefs::shared()` `PropertiesFile`.
  - `persistence/.../SessionFormat.*` — serialize/deserialize per-loop mode; legacy trees default `Add`.
- **Tests:** loop minted under `Over` reports `Over`; default `Add`; SessionFormat round-trips a mixed
  ADD/OVER tree; legacy (no mode) → all-`Add`; prefs defaults.
- **Done when:** mode persists per loop through `promote()` + SessionFormat; both settings read/write;
  `IdaTests` + `IDA` build green.

## Slice 10 — ADD/OVER playback resolution + Output-Mixer track layout
- **Goal:** playback honors per-loop mode; the Output Mixer lays out channels per mode.
- **Files:**
  - `engine/include/ida/RenderPipeline.h` (`activeReadsAt`) + `engine/include/ida/PlaybackResolver.h` —
    **OVER masking:** within a phrase an `Over` loop suppresses all other content of that phrase for its
    `[in,out)` span; `Add` loops layer. **Loop fill:** `Add` loop shorter than its span repeats to fill
    when `addModeLoopFill` is on, else plays once (reuse `RepetitionRules`/`LoopRenderer` `Forever` vs
    `Once`); OVER loops always `Once`. Resolution stays off the audio thread (`PlaybackResolver` worker).
  - `app/MainComponent.cpp` `refreshOutputMixerPhraseChannels()` (~7105) + `OutputMixer`
    `addChannel`/`removeChannel` + Slice 6 per-phrase bus — **ADD loop ⇒ own `T#P#L#` channel**;
    **OVER loop ⇒ no new channel, route through the phrase track / per-phrase bus.** Re-keying on the
    message thread.
- **Tests:** OVER masks an underlying ADD loop in-span; ADD short loop fills vs plays-once per setting;
  channel count = number of ADD loops. Operator-verified: ADD+OVER in one phrase sounds + shows right.
- **Done when:** ADD layers (own channel, optional fill); OVER replaces in-span (shares phrase track).

## Slice 11 — Top-bar ADD/OVER toggle UI  *(operator-verified)*
- **Goal:** global ADD/OVER toggle immediately right of play/pause; label tracks the mode.
- **First step:** amend spec (new §8.8 top-bar mode toggle).
- **Files:**
  - `app/TransportBarHost.{h,cpp}` — own an IDA mode-toggle button; read/write
    `prefs::currentPhraseMode()`; label `ADD`/`OVER`; reflect external (MIDI) changes.
  - **OTTO seam:** play/pause is inside OTTO's `TransportBar`
    (`external/OTTO/src/otto-plugin/ui/components/TransportBar.cpp`, `layoutCompact/Medium/Full` via
    `removeFromLeft`). Add a minimal **accessory-slot hook** to OTTO's `TransportBar` that IDA populates
    immediately after the play/pause button — an OTTO edit under the **Cross-Project Inbox Protocol**
    (`external/OTTO/CROSS_PROJECT_INBOX.md`, `Ida-Origin:` trailer, submodule SHA bump). Keeps the
    phrase-mode concept IDA-owned while honoring the placement requirement.
- **Tests:** operator-verified (position, label flips, click toggles); headless for label-from-mode if
  extracted as a pure helper.
- **Done when:** toggle sits right of play/pause, reads ADD/OVER, switches the global mode Slice 10 honors.

## Slice 12 — Per-phrase MIDI trigger + live MIDI input
- **Goal:** phrases fire from MIDI; each phrase remembers its trigger; mode toggle is MIDI-assignable;
  channel 9 is the control channel.
- **First step:** amend spec — promote §8.5 per-button MIDI from "storage only" to "live trigger +
  per-phrase trigger on channel 9".
- **Files:**
  - `core/include/ida/Phrase.h` `PhraseMetadata` — optional `MidiTrigger { int channel; bool isCc;
    int number; }` (persists with the phrase; reconciles Slice 8 storage so the assignment lives on the
    phrase, not the button).
  - **MIDI input path (new):** establish an IDA MIDI-input handler (standalone + AUv3-safe). Default
    positional map phrase *n* → note/CC *n* on **channel 9**; incoming channel-9 note/CC → dispatch the
    **same source-agnostic command** as Slice 8's button (Slice 5 `IPhraseCommandSink`) so live MIDI and
    button share one path. A channel-9 note/CC also toggles ADD/OVER (Slice 11).
  - Slice 8 phrase-button bank — its **Assign MIDI…** context item writes the phrase's `MidiTrigger`.
- **Tests (headless):** positional map resolves note/CC → phrase index; a fake channel-9 message
  dispatches the identical command a button press would (byte-identical tree, per Slice 5's
  source-agnostic test); non-control-channel ignored; mode-toggle CC flips the global mode.
  Operator-verified: a controller fires phrases + flips ADD/OVER.
- **Done when:** phrases fire from channel-9 MIDI and buttons via one path; each phrase persists its
  trigger; mode toggle responds to MIDI.

## After M13 — Collapse / Expand  *(register against M13; thin consumer of the render path)*
- **Prerequisite:** M13 offline render-through-the-Output-Mixer-FX → file (whitepaper §6.11). Collapse
  consumes it; it does not build the render engine.
- **Files (at execution):**
  - Undoable collapse/expand command on the phrase (phrase-strip / button context menu).
  - Render: sum the phrase's parent data + ADD-loop channels **with each channel's `EffectChain`**
    (`MixerGraphState`/`OutputChannelState.inserts`) into one FLAC clip via the M13 render path.
  - Constituent edit: rendered clip becomes a **temporary stand-in** `TapeReference` on the phrase's
    track; ADD loop `T#P#L#` channels removed (Slice 10 routing in reverse). **Originals retained** (loop
    subtree preserved) so Expand restores subtree + re-adds channels. OVER loops untouched.
- **Done when:** collapse produces one clip honoring per-loop FX; expand fully restores loops + channels;
  round-trip never loses source data.

## Verification (end-to-end)
- **Headless (`ctest --test-dir build`, target `IdaTests`; baseline 449/450):** Slice 9 mode persistence
  + prefs; Slice 10 OVER masking, ADD loop-fill, channel-count-per-mode; Slice 12 positional MIDI map +
  source-agnostic dispatch equivalence.
- **Operator-verified (clean `rm -rf build` first per `[[feedback_clean_builds_only_for_testing]]`;
  Claude builds + launches + numbered steps):**
  1. Record a phrase; ADD-overdub a 2nd loop → both audible, two `T#P#L#` strips, short loop fills
     (toggle play-once → stops filling).
  2. Flip toggle to OVER; overdub a region → replaces that span on playback, shares phrase track, no new
     strip.
  3. A phrase holding both an ADD and an OVER loop behaves correctly (the resolved fork).
  4. Assign a phrase a channel-9 note/CC; fire from controller and from its button → identical; flip
     ADD/OVER from a CC.
  5. (After M13) Collapse a multi-ADD-loop phrase → one FX-baked clip, loop strips gone; Expand →
     restored; source intact.
- **RT-safety:** re-read `docs/RT_SAFETY_CONTRACT.md` before touching `OutputMixer`/`PlaybackResolver`/
  MIDI input on the hot path; mode re-keying and arm flips stay off the audio thread.
