# Render, Parts, and Arrangement — forward direction

Status: vision, not yet specced. This document records the intended direction
so it is not lost. It deliberately does not prescribe an implementation; that
belongs in a dedicated brainstorm → spec → plan session, reconciled with the
V7 milestone roadmap (`docs/superpowers/plans/2026-05-17-v7-alignment.md`).

## The intent

IDA records everything, continuously — but recording everything is not the
same as keeping everything. The capture tapes are the raw, always-on substrate.
The creative act is selecting from that substrate and committing to it.

Once phrases are defined within the tapes, the performer renders those phrases
into keepable **parts** of a song — reusable units ("pills") that can be placed
on a **timeline** and arranged. The timeline is itself rendered into a finished
song. The end state is a full arrangement environment in the spirit of Ableton
Live, reached from a looping-and-capture foundation rather than a linear DAW
one.

This is the throughline from "capture everything" to "deliver a finished work":
capture → define phrases → render phrases to parts → arrange parts on a
timeline → render the timeline to a song.

## Render is a mode that stops the tapes

Render (in this materialize-to-part / bounce sense) is a distinct mode, not the
steady state. When render is invoked, the always-running capture tapes stop.
The system transitions from "recording reality" to "committing a selection of
it to a durable asset." This boundary is the natural seam between the volatile
capture substrate and the kept, arrangeable work.

## Terminology caution

There are two distinct meanings of "render" in this system; keep them named
apart when this work is specced:

- **`engine/RenderPipeline` (existing).** A query: "what should sound at LMC
  time T?" It walks the Constituent graph and reports active tape reads. It
  does not produce a persisted asset and does not stop capture. (M8 S3 added
  render-as-silence for Broken/Invalid Constituents to this path.)
- **Render-to-part / bounce (this document).** An operation that stops the
  tapes and materializes defined phrases into a kept, arrangeable part.

Proposed naming when specced: reserve "render" for the materialize/bounce
operation and refer to the existing query path as the "render query" or
"playback resolution" path, to avoid conflation.

## Open questions for the dedicated session

- What exactly is a "part" as a persisted asset, and how does it relate to the
  existing `Constituent` / phrase model and `core/Arrangement.h` (slots, roles,
  resolution)?
- Does rendering a phrase to a part flatten its effect chain and plug-in state
  (cf. the M8 archival modes: determinism / wet-capture / version-pinning), or
  keep it re-renderable?
- Where does the timeline/clip layer sit relative to the current performance
  and preparation surfaces?
- How does "render stops the tapes" interact with the arm/disarm gesture and
  the always-running tape architecture?
- Placement of all of the above in the V7 milestone roadmap.

## Canonical pointer

The product-level statement of this vision lives in the V7 whitepaper as a
"Future direction" pointer under §6.11 (Export as a first-class destination).
That section currently frames render as "just playback aimed at a file" and
rejects DAW clip-timeline assumptions; this direction must be reconciled with
it during the dedicated design pass. This document is the working home for the
direction and its open questions.
