# I/O ownership and the direct layer — canonical signal-path reconciliation

Design doc. Resolves a contradiction in whitepaper V7 between §5.2/§6.6 (which let an
input channel route "direct to a hardware output") and §7.1 (which states the direct
layer runs "from the input mixer to the output mixer"). It establishes one canonical
rule for which component owns physical I/O, and how a signal travels from a physical
input to a physical output. This is decided model — the "why-it-could-have-gone-another-
way" tier — sitting below the whitepaper (enduring architecture) and above the
implementation specs.

## The ownership invariant

The **input mixer is the sole owner of physical inputs.** Nothing else reads the input
converters.

The **output mixer is the sole owner of physical outputs.** Nothing else writes the
output converters.

Every signal that becomes audible passes through the output mixer. Every captured
signal entered through the input mixer. The two mixers bracket the membrane; the
converters belong to them and to nothing else. This is the hardware-console mental
model — a console's outputs are the console's — and it removes the whitepaper's
self-contradiction rather than enshrining it.

## The three input-to-output paths

A signal reaches a physical output by exactly one of three paths. All three terminate
at a channel in the output mixer; none of them lets the input mixer touch a physical
output.

```
PHYSICAL IN ─▶ INPUT MIXER  (sole owner of physical inputs)
                  │  each input channel, optionally through a bus first:
                  ├─▶ TAPE N         captured — the canonical path
                  └─▶ DIRECT LAYER   uncaptured, stateless bypass (no graph traversal)
                         • raw  = tapped pre-processing   (true sub-millisecond)
                         • proc = tapped post-processing  (a few ms; monitor through your chain)
                         ▼
               OUTPUT MIXER (sole owner of physical outputs)
                  ▲  output channels fed by:
                  ├─ phrases (Constituent renders sourced from tape)
                  └─ direct-layer signals (their own channels — whitepaper §6.6)
                  │  each output channel, optionally through a bus first:
                  └─▶ PHYSICAL OUT
```

1. **Tape path (canonical, captured).** Input channel → optionally a bus → **tape N**.
   The tape is rendered through the Constituent hierarchy into phrases, which arrive at
   output-mixer channels. Full latency; this is the source of truth.

2. **Raw direct (uncaptured, true sub-millisecond).** The input's signal is tapped
   *before* channel processing (an input-layer per-input setting) and carried by the
   direct layer to a dedicated direct-layer channel in the output mixer. That output
   channel is a pass-through, so the path stays sub-millisecond.

3. **Processed direct (uncaptured, a few ms).** The channel's signal is tapped *after*
   its processing chain (a channel-layer per-channel setting) and carried by the direct
   layer to a direct-layer channel in the output mixer, so the performer monitors
   through their own gain/EQ/dynamics.

The direct layer remains what §7.1/§16.8 require: a stateless, audio-thread-exclusive
bypass that is a pure function of input buffers, with no allocation, no locks, and no
graph traversal. It is not part of the data/time architecture and never touches tape.
The only refinement here is its **destination**: it deposits into an output-mixer
channel's buffer (summed to hardware out within the same audio callback — a buffer sum,
not a round trip) instead of writing a physical output directly. Sub-millisecond raw
monitoring is preserved.

## What is rejected

An input channel routing **directly to a physical/hardware output** is rejected. It is
the source of the whitepaper contradiction and it violates the ownership invariant. An
input channel reaches the speakers only via the direct layer terminating at an
output-mixer channel (or via the tape path through phrases). The "real-time monitoring
through the channel's processing" that §5.2 and §6.6 attribute to a direct-to-hardware
route is exactly **processed direct** (path 3) and is preserved by it.

## Implications for the engine (today's code)

These are recorded so the team agrees they are temporary or deprecated, not the target.

- **The input `HardwareOutput` terminal is deprecated for input channels.** The engine
  API (`InputMixer::setChannelMainOutToHardwareOutput`, `setBusMainOutToHardwareOutput`,
  and the `hwNode` accumulation in `renderInputGraph`) stays in place, unused — leaving
  it is non-breaking. It is not exposed in any input-channel destination picker. It is
  removed with the input→output bridge slice.

- **The OutputMixer's one-to-one device-input source is a placeholder.** Today an output
  channel reads the matching physical input channel by index (`OutputMixer.cpp`, the M5
  pre-Constituent proxy). Under this invariant that is a physical input touching the
  output mixer, which is wrong; the code already labels it temporary. Output channels are
  fed by phrases (Constituent renders) or by direct-layer signals — never by raw physical
  inputs. Replacement is M6+ / the bridge slice work, not now.

- **The existing sub-millisecond DirectLayer is untouched for now.** It currently writes
  raw input into the output scratch buffer. Reworking it to terminate at dedicated
  output-mixer direct-layer channels is part of the bridge slice; until then it keeps
  working as-is.

## Sequencing

This invariant is captured now so no code is built against the rejected path. The
near-term tape-UI slice builds only the tape path: a per-input-channel picker that
chooses **which tape** a channel records to (tape destinations only — no "direct" or
hardware-output option). Because that slice offers no way to leave tape, every input
channel necessarily records, so the active "at least one channel routes to at least one
tape" enforcement and the per-channel direct-out opt-out move to the bridge slice, where
a no-tape destination first becomes reachable.

The **input→output-mixer direct-layer bridge** is its own later slice, paired with the
Output Mixer UI (whitepaper Part VI / routing-graph P8): it adds direct-layer channels to
the output mixer, makes output-mixer channels addressable as input-channel destinations,
replaces the OutputMixer device-input proxy, and removes the deprecated input
`HardwareOutput` terminal. It needs the Output-Mixer surface to provide addressable output
channels, so it belongs there rather than ahead of it.

## Whitepaper amendments this doc drives

- **§5.2**, the sentence listing an input channel's destinations as "to a bus, to a tape,
  or direct to a hardware output": reword the third option to routing via the direct layer
  to an output-mixer channel, and fold the "monitoring through the channel's processing and
  inserts" clause into processed direct.
- **§6.6**, "its outputs terminate at tape or at a hardware output": reword so input-mixer
  bus/channel outputs terminate at tape, with monitoring reaching outputs via the direct
  layer to an output-mixer channel — making §7.1 canonical throughout.
