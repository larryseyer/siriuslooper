# Mixer design (sub-project B) — LIVING DOC

Status: **in progress.** Captures decisions as they are made; open questions are
listed at the bottom and resolved one at a time. Sister-app parity with OTTO's
mixer is the visual + functional target (`/Users/larryseyer/AudioDevelopment/OTTO`,
read-only reference).

## Hard invariant: stereo only

**IDA and OTTO work exclusively in STEREO. There is no mono anywhere** — every
channel, bus, send, output, and master is a stereo pair. The mixer never presents
a mono channel or a mono bus. Engine code, routing, and metering all assume
stereo pairs.

The channel is *always stereo internally*. What the operator chooses is each
channel's **input source format** (an input-layer decision, whitepaper §6.2):

- **Stereo source** — two device channels map to the channel's L and R.
- **Mono source** — one device channel, presented dual-mono from the channel
  boundary inward and positioned with **pan**.

Per the **RME TotalMix** convention, a stereo channel **splits** into two
independent mono-source channels and **collapses** back into one. Default is
stereo strips (the common stereo-interface case stays clean); the split/collapse
toggle lives in the channel's settings/detail, never as a permanent button on the
strip face. Each split strip is still a stereo channel internally, fed by one
device input. This keeps the stereo invariant absolute internally while letting a
bank of mono mics and stereo line sources share one console.

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
   a detail panel (pan/**width**, sends, EQ, comp). Per-strip controls (engine):
   gain, pan, **width** (stereo width), mute, solo, dual peak meters — pan/mute/
   solo/metering land in B; width is a small net-new stereo control added with them.

5. **Input Mixer strip layout.** The **input source** (physical/file input +
   processing) occupies the **top** of the channel strip; the **tape-output
   routing** occupies the **bottom**. (Operator: "input on top, output to tape on
   bottom.")

6. **Tape count lives in Settings.** How many tapes the session uses is configured
   in the **Settings** tab, not per-strip. The input mixer routes its channels to
   that pool of tapes.

7. **Counts are independent.** Input-channel count, tape count, phrase count, and
   output count are all independent. Example (operator): 32 stereo input channels
   with only 1 tape enabled; then 32 phrases created and the output mixer set to
   32 stereo outputs — one per phrase. The mixer never assumes these counts match.

8. **Input source format = mono or stereo, RME-style (per channel).** Strips are
   **stereo by default**. The toggle is **universal: stereo = ONE strip, mono =
   TWO strips** (the stereo channel's L and R halves), like RME TotalMix.
   - **True 2-channel pair:** stereo = one strip on device ch (L,R); mono = two
     strips, one per device channel.
   - **Single physical input** (built-in mono mic, or an odd leftover channel):
     stereo = one dual-mono strip (L == R source); mono = **two** strips, both
     carrying that one input's dual-mono signal, independently pannable (e.g.
     hard-pan two copies for width, or process each differently). The toggle
     changes strip count 1↔2 on every input, so it's always visible — even a
     lone mic. (Operator decision: "two strips if mono, one if stereo.")

   The affordance is **gesture-only — right-click (desktop) and long-press
   (touch, 500 ms, drag-cancels)** open a Split/Collapse menu; there is **no
   visible toggle element on the strip or in a detail panel** (the strip is
   already too crowded, especially on iPhone). The channel is stereo internally
   either way (the hard invariant). Engine: a channel carries an **input-source
   descriptor** (1 device channel, or an L/R pair); the input dispatch gathers
   that channel's source channels into a stereo block before
   `ChannelStrip<Audio>::process` (a single channel is dual-mono'd to both sides).

9. **The routing graph: buses, FX returns, sends — dynamically created, both
   mixers.** Full build detail:
   `docs/superpowers/specs/2026-05-20-mixer-routing-graph-design.md`.
   - **Bus vs FX return are distinct nodes, by input source.** A **bus** receives
     **channel (or bus) main-outs** routed into it — a subgroup — and carries
     insert FX (comp/EQ). An **FX return** receives **sends only** (never a direct
     route) and hosts RVB/DLY/plugins — the aux return. Both are a summing node +
     an effect chain + one main-out; they differ only in how signal arrives and in
     typical contents. (Operator: "buses get inputs from channels; FX returns get
     inputs from sends.")
   - **Two signal movements.** **Main-out** (one per node) is a routing assignment
     → a bus or the terminal (tape on input, output·master on output), defaulting
     to the terminal. **Sends** (many, leveled, **post-fader** default) tap
     channels/buses → FX returns only.
   - **Flexible, acyclic, defaults to terminal.** A bus/FX-return main-out is
     re-assignable into another bus (subgroups-of-subgroups, parallel chains); the
     destination picker is **acyclic-enforced**. Most nodes default straight to the
     terminal — "flexible, but defaults to what most people use."
   - **Performer creates them live.** A **blank-area long-press** (extends the
     existing per-strip long-press infra) opens a three-option menu — Input Mixer:
     *Add bus / Add FX return / Add tape*; Output Mixer: *Add bus / Add FX return /
     Add output*. Buses, FX returns, and destinations are unbounded.
   - **Both mixers, shared engine substrate.** Reuses `OutputMixer`'s bus + dense
     send matrix + `Bus`(effect-chain); the input side gets the same substrate
     net-new (terminal = tape). A per-bus **kind** flag (Bus vs FxReturn) and a
     main-out-vs-sends split are the core additions; evaluation order is a
     topological sort recomputed on the message thread.
   - **FX-return contents:** an effect chain — IDA **internal** RVB/DLY (OTTO's
     reverb + delay, a **follow-on spec** right behind this one) **or** 3rd-party
     plugins (UAD, FabFilter…) via the existing out-of-process host. This spec is
     the wiring; the OTTO RVB/DLY integration is the next.

10. **Output-mixer automation model.** Every parameter on every output channel —
    gain, pan, width, EQ, dynamics, send levels, **and the parameters of any
    hosted plugin or built-in insert** — is automatable. Automation is via **mix
    snapshots** (snapshots over continuous automation, WP §6.8), not continuous
    curves.
    - **Per-phrase channel automation is bound to the phrase Constituent** and
      travels with it: **duplicating/copying a phrase produces a new, independent
      copy** of that channel's automation, free to diverge from the original
      (copy-on-write, WP §9.3 / §6.7).
    - **Session-level nodes** (buses, FX returns, master) are **session-bound**,
      not phrase-bound.
    - Cross-ref WP §6.6, §6.8, §9.7. **Design only — no engine work scoped here;**
      automation wiring follows the render-path and routing-graph phases already
      queued.

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

1. ~~Aux/FX send buses (OTTO reverb/delay-style returns): when do they land?~~
   RESOLVED (decision 9 + spec `2026-05-20-mixer-routing-graph-design.md`): they
   land as the dynamically-created routing graph — buses (channel-fed) and FX
   returns (send-fed) on both mixers, built live via the blank-area long-press
   menu. Sequenced as its own multi-phase build after the pan/width slice; the
   internal RVB/DLY (OTTO's) integration follows right behind as a separate spec.
2. Output Mixer meters: built now, go live when the render path is wired (accepted)
   — confirm no interim minimal render path is wanted.
3. Tape-capture wiring: routing inputs→tapes requires wiring `TapeWriter`/
   `TapeStore` into the app (currently unwired), with the store rooted at
   `<IDA>/tapes` (operator request). This is pulled into B's first slice.
