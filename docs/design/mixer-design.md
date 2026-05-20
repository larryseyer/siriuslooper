# Mixer design (sub-project B) — LIVING DOC

Status: **in progress.** Captures decisions as they are made; open questions are
listed at the bottom and resolved one at a time. Sister-app parity with OTTO's
mixer is the visual + functional target (`/Users/larryseyer/AudioDevelopment/OTTO`,
read-only reference).

## Decisions made

1. **Two separate mixers, two tabs.** "Input Mixer" and "Output Mixer" are
   distinct mixers on distinct tabs — never combined on one screen.

2. **Input Mixer.**
   - Channel count = the **selected physical input channels** on the audio device
     (tracks the active inputs chosen in Settings).
   - Contains: input channels, buses, output(s), and per-channel FX + plugins.

3. **Output Mixer.**
   - Channel count = the **created phrases** — one channel per phrase.
   - Contains: input buses, master output(s), and per-channel FX + plugins.

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

## Open questions (resolved one at a time)

1. Input Mixer signal flow: physical inputs → per-channel FX/plugins → buses →
   output(s). What do its **outputs** route to — the physical device out
   (monitoring), the tapes (recording), and/or into the Output Mixer?
2. Buses: how many, fixed vs operator-created, and their purpose (FX sends/returns
   like OTTO's reverb/delay)? Same answer for both mixers or different?
3. Master output(s): a single stereo master, or multiple outputs matching the
   device's output channels?
4. FX/plugins per strip: per-channel insert chain via the existing EffectChain +
   out-of-process host — confirm. Built-in EQ/comp (sub-project C) timing relative
   to B.
5. Output Mixer meters: build the strips now with metering that goes live when the
   render path is wired (accept silent meters meanwhile), or wire a minimal render
   path first?
6. Mute/solo + peak/RMS metering are net-new engine pieces — confirm they land as
   part of B.
