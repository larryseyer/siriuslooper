# Mixer design (sub-project B) — LIVING DOC

Status: **in progress.** Captures decisions as they are made; open questions are
listed at the bottom and resolved one at a time. Sister-app parity with OTTO's
mixer is the visual + functional target (`/Users/larryseyer/AudioDevelopment/OTTO`,
read-only reference).

## Hard invariant: stereo only

**Sirius and OTTO work exclusively in STEREO. There is no mono anywhere** — every
channel, bus, send, output, and master is a stereo pair. The mixer never presents
a mono channel or a mono bus. Engine code, routing, and metering all assume
stereo pairs.

## Decisions made

1. **Two separate mixers, two tabs.** "Input Mixer" and "Output Mixer" are
   distinct mixers on distinct tabs — never combined on one screen.

2. **Input Mixer — the capture console.**
   - **Inputs are physical/file inputs ONLY. It NEVER takes a tape as an input —
     it OUTPUTS to tapes.** (Critical: tapes are downstream of this mixer.)
   - Channel count = the **selected physical/file input channels** (tracks the
     active inputs chosen in Settings).
   - Signal flow: inputs → per-channel processing (gain, pan, FX, plugins) →
     **route single or combined inputs to a user-selected number of tapes**. The
     operator chooses how many tapes and which input(s) land on each; multiple
     inputs may be combined onto one tape.
   - Its destinations are **tapes** (stereo), the recording targets — not the
     speakers.
   - **First buildable slice includes the output-to-tapes routing** (per the
     operator: "start on input mixer and include its output to the user-selected
     number of tapes").

3. **Output Mixer — the mixdown console.**
   - Channel count = the **created phrases** — one channel per phrase.
   - Signal flow: each phrase channel → per-channel processing (FX, plugins,
     gain, pan) → route each phrase to **its own stereo output**, or **combine
     into a stereo bus**, or **combine into a single stereo master** output.
   - Per-channel FX + plugins apply here too (same insert model as the input
     mixer).
   - Its destinations are stereo **outputs / buses / master**.

4. **Look & feel.** OTTO's mixer visuals — `CompactFaderStrip` + `FaderMeter`
   ported from OTTO; OTTO `LookAndFeel` already vendored (sub-project A). Strip
   anatomy follows OTTO: name header, mute/solo, fader + dual meter, routing, and
   a detail panel (pan/width, sends, EQ, comp).

## Engine reality (from the GUI seam audit)

- Audio flows live today: device-in → `InputMixer` (gain/pan) → `Bus`/`OutputMixer`
  → device-out. So **Input Mixer meters can show real signal immediately.**
- `OutputMixer` comes up with **no channels**; "one channel per phrase" requires
  registering a channel per phrase AND routing phrase audio through it. Phrase
  audio comes from the render pipeline (loops → audio), which is **NOT wired into
  the audio callback yet** (`RenderPipeline` is query-only). So the **Output Mixer's
  per-phrase meters stay silent until the render path lands** — the strips and
  controls are built now; live metering follows.
- Net-new engine work required for B regardless: **peak/RMS metering** (compute in
  the mixers' render, publish via atomics, UI reads at ~30 Hz) and **mute/solo**
  (`ChannelStrip` currently has gain/pan only).
- Reusable as-is: per-channel gain/pan (`ChannelStrip`), bus routing + sends
  (`OutputMixer::addBus`/`routeChannelToBus`/`sendLevelFor`), effect-chain hosting
  (`OutOfProcessEffectChainHost`).

## Resolved

- **Build order:** Input Mixer first (live meters now), Output Mixer after the
  render path is wired.
- **Input Mixer destinations:** tapes (stereo); inputs are physical/file only.
- **Output Mixer destinations:** per-phrase stereo output, stereo bus, or single
  stereo master — all stereo.
- **FX/plugins:** per-channel insert chains on both mixers (via the existing
  EffectChain + out-of-process host). Built-in EQ/comp DSP is sub-project C.
- **Net-new engine for B:** peak/RMS metering (compute in render, publish via
  atomics, UI reads ~30 Hz) and mute/solo (stereo).
- **Everything stereo** (see the hard invariant above).

## Still open / to confirm while building

1. Aux/FX send buses (OTTO reverb/delay-style returns): when do they land relative
   to the core strips? (The input→tape "combine" and output stereo-bus/master are
   distinct from FX-return buses.)
2. Output Mixer meters: built now, go live when the render path is wired (accepted)
   — confirm no interim minimal render path is wanted.
3. Tape-capture wiring: routing inputs→tapes requires wiring `TapeWriter`/
   `TapeStore` into the app (currently unwired), with the store rooted at
   `<Sirius>/tapes` (operator request). This is pulled into B's first slice.
