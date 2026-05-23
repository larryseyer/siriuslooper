# IDA colour method

How IDA assigns colours to tapes, phrases, loops, and timeline pills. The goal
is sister-app parity with OTTO and a single, predictable rule the operator can
rely on at a glance.

## Palette source

All colours come from **OTTO's eight player hues** (`otto::Colours::getPlayerColour`,
vendored in `ui/lookandfeel/OTTOColours.h`): orange, magenta, purple, green,
orange, lavender, leaf-green, sky-blue. This is the shared palette both apps draw
from, so IDA and OTTO feel like one family.

The single source of truth for the rule is `ui/include/ida/IdaPalette.h`.

## Keying: stable id, not screen position

A colour is selected from the eight hues by the entity's **stable id**, folded
into range (`id mod 8`) — never by its row order on screen. Consequences:

- The same phrase is the same colour in the structural tree **and** as its pill on
  the timeline.
- A tape keeps its colour when rows are reordered; a phrase keeps its colour when
  the tree is edited.

`hueForId(id)` implements this; `tapeColour(tapeId)` and `phraseColour(phraseId)`
are the named entry points.

## The four entities

- **Tape** (timeline row) — its own hue, keyed by `TapeId`. Drawn as the coloured
  band on the left edge of the row strip.
- **Phrase** — its own base hue, keyed by `ConstituentId`. Drawn as the tree row's
  text colour and as its pill's border on the timeline.
- **Loop within a phrase** — a **shade** of its parent phrase's hue, stepped by the
  loop's order among its siblings (`loopShade(phraseHue, order)`), so a phrase and
  its loops read as one colour family while staying distinguishable. The step uses
  small alternating brighter/darker deltas that stay legible on the dark
  background and never wash out to white/black.
- **Pill** (a phrase placed on the timeline) — the phrase's hue: a dark fill of the
  hue with the bright hue as the border (OTTO's pill treatment).

## Why id-keyed hues can collide

Tapes and phrases draw from the same eight hues by different ids, so a tape and a
phrase can coincidentally share a hue. That is acceptable: they live in different
contexts (a tape is a row band; a phrase is a pill / tree row), and eight hues
keep the common small sessions distinct. If a future session routinely exceeds
eight tapes or phrases and collisions become confusing, extend the palette or add
a per-category offset here — this file is the one place to change it.
