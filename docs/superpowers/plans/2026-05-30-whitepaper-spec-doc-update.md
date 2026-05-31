# Whitepaper + Spec Documentation Update — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or superpowers:subagent-driven-development) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align `docs/IDA_Whitepaper_V10.md` with the resource-aware capture model — *the clock always runs; a tape records iff an input is assigned to it* — without retreating from the paper's "capture every inspirational moment" intent.

**Architecture:** Doc-only edits. The committed design spec (`docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md`, §12) is the source of these changes; this plan applies them to the whitepaper and finalizes status. No code.

**Tech Stack:** Markdown. Verification is by `grep`/read, not unit tests. Whitepaper line numbers drift as edits land — **locate each edit by its quoted phrase, not by line number.**

---

### Task 1: Add the resource-aware-capture subsection to the whitepaper

**Files:**
- Modify: `docs/IDA_Whitepaper_V10.md` (insert near the end of Part VII / start of Part VIII — after the §7.2 MON/DIR material, before the tape's append-only details)

- [ ] **Step 1: Locate the insertion point**

Run: `grep -nE "Part VIII|append-only|§7\.2" docs/IDA_Whitepaper_V10.md | head`
Pick the heading that begins the tape's Part VIII detail; insert the new subsection immediately before it.

- [ ] **Step 2: Insert the subsection**

Insert this prose verbatim:

```markdown
### Resource-aware capture: the tape runs where a source is assigned

IDA's aim is to capture every inspirational moment a performer has. It pursues
that aim without wasting the user's resources, by separating two layers that have
historically been conflated under the word "arm":

- **The clock is unconditional.** The Logical Master Clock runs continuously and
  is the only honest timebase; it never stops.
- **Disk capture is conditional.** A tape writes to disk *if and only if* at least
  one input is assigned to it. A tape with no assigned input does not run, and
  therefore consumes no disk. Assigning an input arms its tape; removing the last
  input stops it.

Retroactive capture is preserved within this rule: an assigned tape keeps its
retroactive ring and lossless on-disk stream from the moment of assignment, so a
phrase may still be marked a beat after it was played — back to the assignment
point. Before assignment there is deliberately no data, because spending disk on
sources no one has routed is the waste this rule eliminates. "Always-running"
thus means "running wherever a source is assigned," and the tape remains the
source of truth for everything that was captured.
```

- [ ] **Step 3: Verify**

Run: `grep -n "Resource-aware capture" docs/IDA_Whitepaper_V10.md`
Expected: one match.

- [ ] **Step 4: Commit**

```bash
git add docs/IDA_Whitepaper_V10.md
git commit -m "docs: whitepaper — add resource-aware capture subsection"
```

### Task 2: Scope the "always-running source of truth" prose anchors

**Files:**
- Modify: `docs/IDA_Whitepaper_V10.md` (the prose sentences that assert the tape is "always-running")

- [ ] **Step 1: Enumerate the occurrences**

Run: `grep -nE "always[- ]running" docs/IDA_Whitepaper_V10.md`
Expected anchors (drifted): the intro (~27), the engine three-stage description (~334), the "deliberate symmetry" paragraph (~428), the mermaid tape node (~443), the signal-path summary (~592), the glossary entry (~1977), and the numbered principle (~1999).

- [ ] **Step 2: Add the scoping clause at each prose occurrence**

For each sentence asserting the tape is the "always-running source of truth," append (or weave in) this clause once per sentence — do **not** alter the diagram node text mechanically beyond adding "while assigned":

> "— running while an input is assigned; the LMC, not the tape, is the unconditional always-on element (see *Resource-aware capture*)."

For the mermaid node (~443) and any diagram label, change the inline note `always running` → `running while assigned`.

- [ ] **Step 3: Verify no bare claim remains**

Run: `grep -nE "always[- ]running" docs/IDA_Whitepaper_V10.md`
Expected: every remaining hit is either the new subsection (Task 1) or a sentence that now carries the scoping clause / "while assigned." Read each to confirm.

- [ ] **Step 4: Commit**

```bash
git add docs/IDA_Whitepaper_V10.md
git commit -m "docs: whitepaper — scope always-running tape to assigned sources"
```

### Task 3: Fix the remaining specific anchors (table, retroactive, disk)

**Files:**
- Modify: `docs/IDA_Whitepaper_V10.md`

- [ ] **Step 1: Comparison-table cell**

Run: `grep -n "Not required; tape is always running" docs/IDA_Whitepaper_V10.md`
Change the cell text to: `Not required; the tape runs while its input is assigned`.

- [ ] **Step 2: Retroactive-grab passage**

Run: `grep -n "the moment is already on the tape\|moment is already on" docs/IDA_Whitepaper_V10.md`
In that paragraph, change "Because tapes are always running …" to "Because an assigned tape runs from the moment of assignment …" and add: "the reach-back extends to the assignment point, not before it."

- [ ] **Step 3: Disk-cost passage**

Run: `grep -n "gigabyte per hour per source" docs/IDA_Whitepaper_V10.md`
In that paragraph, add a sentence: "The primary control on this cost is not recording unassigned sources at all; lossless-on-disk is the secondary control for the sources that are assigned."

- [ ] **Step 4: Verify + commit**

Run: `grep -nE "always running|gigabyte per hour" docs/IDA_Whitepaper_V10.md` and read the hits.
```bash
git add docs/IDA_Whitepaper_V10.md
git commit -m "docs: whitepaper — scope retroactive grab and disk-cost passages"
```

### Task 4: Finalize spec status and update the overturned-invariant memory

**Files:**
- Modify: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` (Status line)
- Modify: `/Users/larryseyer/.claude/projects/-Users-larryseyer-IDA/memory/project_looper_at_least_one_tape_invariant.md` and its `MEMORY.md` index row

- [ ] **Step 1: Mark the spec approved**

Change the Status line to: `Status: approved — see implementation plan docs/superpowers/plans/2026-05-30-blank-slate-first-run-implementation.md`

- [ ] **Step 2: Correct the looper-invariant memory**

The memory `project_looper_at_least_one_tape_invariant` is overturned by this design (zero tapes is the legal empty state; tapes record iff assigned). Rewrite that memory file's body to state the new rule and reference the spec, and update its one-line hook in `MEMORY.md`. Do **not** delete the file — replace the now-false claim with the corrected one.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md
git commit -m "docs: spec — mark approved; point at implementation plan"
```
(The memory directory is outside the repo; it is saved by the Write tool, not committed.)

---

## Self-review

- **Spec §12 coverage:** Task 1 = new subsection; Task 2 = the seven prose "always-running" anchors; Task 3 = table cell (129), retroactive (842), disk (856). Line 588 ("Tape topology is mixer topology") is reinforced, not edited — intentionally no task. ✓
- **No placeholders:** every step has the literal phrase to find and the literal text to write. ✓
- **No code:** doc-only, as scoped. ✓
