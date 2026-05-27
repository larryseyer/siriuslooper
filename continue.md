# Session Continuation — Output Mixer pane surgical-append parity landed; OTTO-is-transport insight captured

## ▶ 0. Read these first (60 seconds)

1. **`OutputMixerPane` now matches `InputMixerPane`'s d01bd00 surgical-append
   pattern.** Two new methods (`appendPhraseStrip`, `appendMonStrip`) plus
   pure-append fast paths in `refreshOutputMixerPhraseChannels` and
   `refreshOutputMixerMonChannels`. When the timeline delta is "N new pills
   added at the end" (no removes, no reorder), the per-tick refresh appends
   strips surgically instead of rebuilding the whole row. Existing strips'
   fader / mute / sends / MON state survives intact. Other deltas (remove,
   reorder, mixed) still hit the full `setPhraseStrips` / `setMonStrips`
   rebuild path — that's symmetric with the Input side, which also rebuilds
   on file-input removal (`onRemoveFileInputRequested` →
   `rebuildInputStrips`).
2. **OTTO is the transport source for IDA.** Operator clarified this mid-
   session. The earlier "(B) Transport sync" follow-on assumed IDA's LMC
   published transport state — it does not. `engine/include/ida/Lmc.h` is
   pure time (`nowSeconds`, `advanceBySamples`); no `isPlaying` / start /
   stop / `EngineTransport` class exists anywhere in `engine/`, `audio/`,
   `core/`, or `app/`. **All three previously-queued follow-ons (B Transport,
   C MIDI, D Video) are now blocked on OTTO import.** New memory captures
   this: `project_otto_is_the_transport_source`.
3. **Baseline.** `master` at `<HEAD after this session's commits>`. Confirm
   with `git log -1 --oneline` and `git status --short`.
4. **Test surface.** `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j`
   → **776 / 776** on clean rebuild. Allow up to 4 transient flakes
   (identity varies — `--rerun-failed` clears them; observed both before
   and after this slice).
5. **Operator eyes-on still pending for this slice.** Pass criterion: open
   the Output Mixer tab, adjust the fader / mute / sends on a phrase strip,
   then add another phrase in the Songwriter tab (which triggers
   `refreshOutputMixerPhraseChannels` → pure-append path). The first
   phrase strip's values must survive. Pre-fix they would be reset to
   defaults; post-fix they survive. Same test for MON strips: flip MON on
   for one more input strip while a different MON strip's fader is mid-
   adjust.

---

## ▶ 1. What landed THIS chat

| File | Change |
|---|---|
| `app/MainComponent.cpp` | `OutputMixerPane::appendPhraseStrip(info)` + `OutputMixerPane::appendMonStrip(info)` added; `refreshOutputMixerPhraseChannels` + `refreshOutputMixerMonChannels` detect pure-append deltas and use the surgical methods |
| `todo.md` | B/C/D reclassified — transport sync hard-blocked on OTTO; MIDI + Video softly deferred until OTTO is in (so their transport-sync sub-features can be designed once). Stale player-polish entry pruned (already shipped). |
| `continue.md` | This file. |
| memory `project_otto_is_the_transport_source` | Captures the OTTO-is-transport architectural fact + the empirical confirmation that LMC has no transport state. |

No deferred items landed in `todo.md` from this session's surgical-append
work itself — the slice closes cleanly.

---

## ▶ 2. Architecture notes worth carrying forward

### Note A — Engine-side surgical add/remove was already present

`refreshOutputMixerPhraseChannels` already does delta detection at the
engine layer (lines around `MainComponent.cpp:6261-6295`): it computes
`toRemove` and `toAdd` against `phraseChannelByConstituent_` and calls
`outputMixer_->addChannel` / `removeChannel` surgically. **Only the
UI rebuild was the wipe-everything step.** This session brings the UI
into alignment with what the engine already did.

### Note B — Pure-append detection shape

```cpp
const bool pureAppend
    = newOrder.size() > oldSize
   && std::equal (old.begin(), old.end(), newOrder.begin());
```

Reusable shape for any "is this delta a pure end-append?" check — works
on any vector whose element type has `operator==`. Used twice in this
slice (phrase row + MON row).

### Note C — The lambda-capture-by-index constraint

`appendPhraseStrip` / `appendMonStrip` set up `onClick` lambdas capturing
`idx = phraseStrips_.size()` at append time. Those lambdas remain valid
as long as the strip stays at that row index. **That's why we only do
surgical append at the end**, not surgical remove or reorder — removing a
middle strip would invalidate every subsequent strip's captured `idx`,
which would silently misroute clicks. The Input side has the same
constraint (see `InputMixerPane::appendStrip` at line ~800); removes go
through full `rebuildInputStrips`.

### Note D — OTTO is a three-way dependency

OTTO (when imported) supplies IDA with:
- **Transport** (this session's insight, `project_otto_is_the_transport_source`)
- **32 stereo audio outputs as Output Mixer channel strips**
  (`project_otto_as_output_mixer_source`)
- **Internal FX (EQ/CMP/RVB/DLY) consumable from the submodule**
  (`project_internal_fx_first_class`)

All three depend on "OTTO importable into IDA" as a single milestone. The
surgical-append pattern landed in this slice happens to be exactly the
UI seam OTTO's 32 outputs will use — those strips will arrive via
`appendPhraseStrip`-style calls without nuking the rest of the row.

### Note E — Phrase + MON right-click was considered, dropped

The exploration suggested mirroring the aux-bus `StripContextOverlay`
("Rename…") onto phrase + MON strips. Dropped because:
- **Phrase names come from the constituent tree** — renaming a phrase is
  an undo-stack mutation on the model, not a mixer rename like buses are.
  Substantial design work; out of scope for a parity slice.
- **MON strips have auto-generated names** ("MON 1", "MON 2") derived
  from the input row order. Not operator-meaningful to rename.

If the operator later wants right-click affordances on these strips, the
useful entries are different — e.g. "Turn MON off on source channel" for
MON strips, "Show in Songwriter" for phrases. Separate design slice.

---

## ▶ 3. What's next

The three follow-ons (B/C/D) are now blocked on OTTO import. So the
question shifts to **what unblocks OTTO import**. From the existing
memories:

1. **OTTO consumed as submodule** ✓ (`project_otto_is_a_submodule_now`).
2. **OTTO assets path** ✓ (`project_otto_assets_out_of_git` — `OTTO_ASSETS_DIR`
   wired).
3. **OTTO's 32 stereo outputs presented as Output Mixer channel strips**
   — `project_otto_as_output_mixer_source`. The surgical-append seam
   from this slice is the UI side of this. Engine side is the work that
   bridges OTTO's output buses into IDA's `OutputMixer::addChannel`
   per-OTTO-output.
4. **Internal FX consumed from OTTO submodule** —
   `project_internal_fx_first_class`. EQ/CMP/RVB/DLY adapters bound to
   OTTO's header-only DSP.
5. **OTTO's transport API surfaced to IDA** — the seam this session's
   memory captures. Likely a listener / callback subscription on the
   bundled OTTO instance.

Suggested next slice options (operator picks):

### (A) OTTO integration scope doc — design first
Step back and write `docs/superpowers/specs/<date>-otto-integration-scope.md`:
enumerate what "OTTO importable into IDA" means as a milestone, in what
order the pieces land, what each piece blocks/unblocks. Produces a
roadmap doc; no code lands. Then pick the first OTTO sub-slice from
that doc next session.

### (B) Output Mixer engine seam for OTTO 32-output ingestion
Pure engine work: add a method (`outputMixer_->addOttoOutput(idx)` or
similar) that adds a stereo-pair channel sourced from an external audio
pointer (the OTTO bundle's output bus index). No UI yet — UI surgical-
append is already done.

### (C) Investigate OTTO's transport surface
Read OTTO's source in `external/OTTO/` and identify the API IDA will
subscribe to. May surface a request via the cross-project inbox
(`external/OTTO/CROSS_PROJECT_INBOX.md`) if OTTO doesn't publish a
clean transport-state listener yet.

### (D) A different IDA-side gap entirely
If the operator wants more IDA-only polish before turning to OTTO, the
candidates are: Note E from the previous session (session-load
hardware-state regression — small focused refactor), or another
operator-named gap.

Default recommendation: **(A) OTTO integration scope doc.** It sets the
roadmap for the next few slices and gives the operator one place to
direct the sequencing rather than picking subslices ad-hoc.

---

## ▶ 4. Baseline as of this handoff

| Check | Result |
|---|---|
| Branch | `master`, local == origin (after this session's push) |
| HEAD | (verify with `git log -1 --oneline` — should be the continue.md commit) |
| `git status --short` | clean (after commits) |
| `ctest --test-dir build -E "(PluginEditor\|MainComponentPlug)" -j` | **776 / 776** on clean rebuild; up to 4 transient flakes pass on `--rerun-failed` |
| `./build/tests/IdaTests "[file-input]"` | (not retouched this slice — should still pass) |
| Operator eyes-on (pending) | (1) phrase-strip fader / mute survives a refresh tick that adds another phrase. (2) MON-strip fader survives flipping MON on for one more source. Both were the regression class d01bd00 fixed on the input side; this slice closes the equivalent on the output side. |

---

## ▶ 5. Resume protocol for next chat

1. Read this file (you're doing it).
2. If the operator hasn't yet eyes-on'd the surgical-append change, spot-
   test that — add a phrase, mute another phrase, confirm the muted
   phrase survives. Same for MON.
3. Pick a "what's next" from §3 above. Default is (A) OTTO integration
   scope doc, but the operator's standing answer is "most professional
   and elegant" — for a multi-piece integration, that's a roadmap doc
   before code.
4. Update memory if anything new surfaces about OTTO's API or the
   integration sequence.

If the operator wants to fix Note E from the *previous* session
(session-load hardware-state regression at `app/MainComponent.cpp:~7745`),
that's a small focused refactor independent of OTTO — replace
`rebuildInputStrips()` inside the file-input load block with a loop that
calls `appendStrip` per loaded file input (mirror of the
`onAddFileInput` pattern from d01bd00). Single commit, no spec needed.

Reference docs:
- `project_otto_is_the_transport_source` (this session's new memory)
- `project_otto_as_output_mixer_source` + `project_otto_is_a_submodule_now`
  + `project_internal_fx_first_class` — the OTTO-integration triad
- `project_input_output_mixers_identical` — the parity rule this slice
  served
- d01bd00 commit — the canonical surgical-append template
- Whitepaper: `docs/IDA_Whitepaper_V9.md`
- OTTO submodule: `external/OTTO/`; cross-project inbox at
  `external/OTTO/CROSS_PROJECT_INBOX.md`

---

*End of session. Output Mixer pane gains surgical-append parity with the
Input side; the OTTO-is-transport architectural truth is captured in
memory and reflected in todo.md. Three follow-ons (B/C/D) are now
correctly blocked on OTTO import rather than masquerading as queued-and-
ready.*
