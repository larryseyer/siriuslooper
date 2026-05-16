# Sirius Looper — User Guide

The operator's manual. Workflow-first: each chapter walks through
something you can actually do with Sirius. Read in order if you're new;
jump to the chapter you need otherwise.

For the conceptual model, design philosophy, and architectural rationale,
see the white paper at `Sirius Looper Whitepaper V2.md` (same folder).
The white paper says *why* the system works the way it does. This guide
says *how* to make music with it.

---

## Glossary

These terms appear throughout the guide. Pin them down once here.

| Term            | Meaning                                                                                                       |
|-----------------|---------------------------------------------------------------------------------------------------------------|
| **Tape**        | The always-running recording of one input. Every audio input has a tape that is continuously capturing while Sirius is open. Tapes are infrastructure — you usually don't see them in the normal view. They make retroactive capture possible. |
| **Input**       | One audio source. A guitar plugged into channel 1, a microphone, a synthesizer plugged into channel 2 — each is an input, and each input has its own tape. |
| **Phrase**      | A musical thought you've captured. A verse, a chorus, an intro, a fill. A Phrase is a container; it holds Loops (and can hold sub-Phrases). Phrases carry musical meaning — role, intent, how they begin and end. |
| **Loop**        | A specific slice of a tape that's part of a Phrase. The actual audio. Loops are leaves — they don't contain anything else; they point at a slice of audio on a specific tape. Every Loop lives inside a Phrase. |
| **Pill**        | The colored rounded shape on the timeline. Each Pill is the visual rendering of one Phrase. |
| **CaptureRegion** | The temporary bookmark made by a Mark In / Mark Out gesture. Internal — you don't normally see it. The system immediately turns it into a Loop (and a Phrase, if needed). |
| **Mark In**     | The gesture that marks the *start* of a captured region. |
| **Mark Out**    | The gesture that marks the *end* of a captured region. Auto-promotes the captured region into the song. |
| **Arm**         | Standing the system up so capture gestures take effect. Disarmed = nothing captures, no surprises. Armed = the next Mark In / Mark Out will land. |
| **Playhead**    | The current time position. Where you are in the song. |
| **LMC**         | "Local Machine Clock" — the system's continuous time reference. The unit on the Tape rulers. You don't usually need to think about LMC; the playhead and the song timeline handle this for you. |

---

## Chapter 1 — Capturing Phrases and Loops

### The capture gesture

Every capture is the same three-step sequence:

1. **Arm** — tap the Arm button (or the per-input Arm in the Preparation
   tab). The button turns red. The system is now listening for capture
   gestures. Until you arm, nothing captures — Sirius starts disarmed
   on purpose, so you never capture by surprise.

2. **Mark In** — at the moment you want the captured region to start,
   tap Mark In. The system records the playhead position as the
   in-point. Visually, you'll see Mark Out become available.

3. **Mark Out** — at the moment you want the captured region to end,
   tap Mark Out. The system closes the region, **and immediately
   promotes it** into the song.

You can re-tap Mark In before Mark Out — that *replaces* the in-point
without losing the armed state. You can also switch which input you're
capturing onto in between Mark In and Mark Out, by tapping a different
input's row.

### What Mark Out does — the auto-promotion rule

When Mark Out fires, Sirius decides what to do with the captured region
based on the playhead position at the moment of **Mark In**:

- **Mark In was outside any existing Phrase** → Sirius mints a *new
  Phrase* containing the captured Loop, and adds it to the song. A
  new Pill appears on the timeline.

- **Mark In was inside an existing Phrase's span** → Sirius adds the
  captured Loop *as a child of that Phrase*. No new Pill — the existing
  Pill quietly gains another layer.

This is the rule for **everything**. There's no "create phrase" button
versus "create loop" button. The same physical gesture (Mark In → Mark
Out) means different things based on whether you're inside an existing
Phrase or not.

#### Diagram — outside any Phrase

```
Timeline (empty so far):
   0s    4s    8s   12s   16s   20s
   ┃     │     │     │     │     │
         Mark In here       Mark Out here
         (no Phrase exists)

Result:
   0s    4s    8s   12s   16s   20s
   ┃     ╭─────new Phrase─────╮
         │  (with one Loop)   │
         ╰────────────────────╯
```

#### Diagram — inside an existing Phrase

```
Timeline (a Verse Phrase already exists):
   0s    4s    8s   12s   16s   20s
   ┃     ╭─────────Verse──────────╮
         │                         │
         Mark In here  Mark Out here
         (inside Verse)

Result:
   0s    4s    8s   12s   16s   20s
   ┃     ╭─────────Verse──────────╮
         │       (now with        │
         │        one more Loop)  │
         ╰─────────────────────────╯
```

### When the captured region straddles a Phrase boundary

If your Mark In was inside a Phrase but your Mark Out lands past the
Phrase's end (or vice versa), **Mark In wins** — the captured region is
treated as belonging to the Phrase that contained Mark In. The Loop's
boundaries are clamped to fit within the host Phrase.

The reasoning: Mark In is the moment you committed to the capture; Mark
Out is just where you stopped. If you intended a different Phrase as the
host, hit Undo and try again with the playhead in the right place.

### Recovering from an early Mark Out

If you hit Mark Out by accident, too soon, or you don't like what just
landed, **Undo is non-destructive of your capture state**. Two ways to
undo:

- **Tap the CaptureBanner** that just appeared. The banner shows what
  was captured (`Loop added to Verse · 3.6 s · tape #200` or
  `Phrase 3 captured · 3.6 s · tape #200`) and includes a small `↶ Undo`
  affordance on the right. Tap it within the 1.5-second window the
  banner is visible.

- **Tap the bottom-bar Undo button**. Always works.

After undoing a promotion, Sirius restores the capture session to its
state *before* Mark Out: your Mark In is intact, the system is armed and
awaiting an out-point. The tape never stopped recording — it has all the
samples between Mark In and where you (mistakenly) marked out, and
beyond. So you can immediately tap Mark Out again at the right moment,
or tap Disarm to abandon the capture entirely.

In short: hitting Mark Out early costs you a single tap to recover. It
does not cost you the capture itself.

### Building a song from one instrument, then layering instruments on top

A natural workflow that uses the auto-promotion rule end-to-end.

#### Pass 1 — lay down the song structure on one instrument

Plug a guitar into input 1. Arm input 1.

Play through the song's sections, one at a time, with Mark Out at the
end of each:

```
0s ──── play intro ──── Mark Out  (Phrase 1: Intro)
       ──── play verse ──── Mark Out  (Phrase 2: Verse)
       ──── play chorus ──── Mark Out  (Phrase 3: Chorus)
       ──── play bridge ──── Mark Out  (Phrase 4: Bridge)
                                        ...
```

Each Mark Out is outside any existing Phrase (you're building forward
in time on a fresh timeline), so each mints a fresh Phrase. By the end
of Pass 1 you have a row of Phrases on the timeline, each with one
Loop child pointing at the guitar tape.

#### Pass 2 — overdub layers

Plug a bass into input 2. Arm input 2. Scrub the playhead back to
song-time zero. Play along through the song.

As the playhead crosses each Phrase's span, hit Mark In (start of bass
part) and Mark Out (end of bass part). Because Mark In is now *inside*
an existing Phrase, each capture lands as a Loop child of that Phrase
— no new Pills appear, but each Phrase quietly gains a bass layer.

Repeat for piano, drums, vocals, or anything else. Each pass adds Loops
to the existing Phrases.

### What's deferred (Roadmap)

- **Repeating song sections.** The verse plays three times. Recording
  into a verse adds to every verse — that's the default. Hold the Mark
  In button for a moment to record into just one. If a verse needs to
  drift on its own ("Vary this one" from its menu), it stops being tied
  to the others from that point on. The timeline shows you which is
  which: a tie above the pills means "the same verse," a dot means
  "something just for this one," a small mark means "this one is its
  own thing now." A full chapter on this lands once the gesture feels
  right in real use.
- **A normally-hidden tapes view** with an explicit "show tapes"
  affordance. Today the Preparation tab still surfaces tape rows by
  default. Future versions move tapes underneath a phrase-centric
  primary view.
- **Naming and editing Phrases.** Today minted Phrases get a default
  *role* of `capture` and an empty name; editing the name / role / intent /
  grammar is a future Preparation-pane feature.
- **Capture-history widget.** Superseded by auto-promotion — captures
  are now Pills on the timeline, which serve the same need as a
  history list.
