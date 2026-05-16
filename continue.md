# Session Continuation — 2026-05-16 (shared-placement spec landed; next = writing-plans)

> **For a fresh chat picking this up cold:** read this whole file before
> doing anything. The user's `~/.claude/CLAUDE.md` and the project's
> auto-memory (`MEMORY.md` + `*.md` in the memory dir) are loaded
> automatically and contain the rules. This file is the *state*: what
> just happened, what is ready to verify, and what to start on next.

---

## 0. Quick orientation

**Sirius Looper** is a real-time looping / arrangement application for
musicians, built in JUCE/C++20 with a strict separation between a
JUCE-free conceptual-time core (`core/`) and the audio/UI layers
(`engine/`, `ui/`, `app/`). Sister app to **OTTO**.

Authoritative reading, in order if you need it:

1. **`/Users/larryseyer/.claude/CLAUDE.md`** — global engineering
   rules. Already auto-loaded.
2. **`docs/superpowers/specs/2026-05-16-shared-placement-design.md`**
   — *the* spec the next session implements a plan for. Read it
   start-to-finish. 14 numbered design sections; 11 work items in the
   "Dependencies" block at the end.
3. **`docs/superpowers/plans/2026-05-15-capture-promotion.md`** — the
   prior session's plan. The new plan mirrors its structure: bite-
   sized TDD tasks, exact file paths, exact code, frequent commits.
   Prior plan delivered the prior pick across 17 commits — expect
   similar shape here.
4. **`docs/superpowers/specs/2026-05-15-capture-promotion-design.md`**
   — capture-promotion design. §3 is the runtime guard the new design
   replaces; §1 is the Phrase-vs-Loop convention the new design
   preserves.
5. **`docs/Sirius Looper Whitepaper V2.md`** — conceptual model. Parts
   VII (Constituent unifying abstraction) and IX (arrangement) are
   the load-bearing background.
6. **`docs/Sirius Looper User Guide.md`** — operator-facing how-to.
   The new spec §13 sketches a future "Repeating song sections"
   chapter that lands once implementation ships.
7. **`todo.md`** — deferred-work register. The original shared-
   placement entry (line ~42) is now marked **SUPERSEDED** by the
   spec; that's the only post-this-session change. The implementation-
   plan and implementation itself will produce subsequent entries as
   work decomposes.

**Project policies that override defaults** (full text in CLAUDE.md
and in the auto-memory):

- **Work directly on `master`.** No feature branches unless asked.
- **Commits AND pushes are authorized.** Claude commits to master and
  pushes to `origin/master` as deliverables land. No PRs (trunk push
  only). No `--force`. No `--no-verify`. See memory
  `feedback-claude-commits-and-pushes-master`. `bash/bu.sh` is the
  user's *local* backup tool — Claude does not run it.
- **Single-line commit messages**, format `<type>: <short title>`. No
  Co-Authored-By trailer.
- **Never run `open ...` or any GUI-launching command.** Operator-side
  .app verification is something the *user* runs at their terminal.
- **Defer big design topics to their own session** rather than letting
  them bloat the current one. See memory
  `feedback-defer-big-design-to-own-session`. **This is why the
  implementation plan is a separate session from the spec.**

---

## 1. What just shipped (last session, 2026-05-16, Pick B brainstorm)

**Pick B brainstorm — shared-placement spec.** Three commits on
master, pushed to `origin/master`:

| SHA       | Subject                                                                          |
|-----------|----------------------------------------------------------------------------------|
| `81afadd` | docs: spec — shared-placement architecture (Pick B)                              |
| `390e46e` | docs: handoff — spec landed, next session writes the implementation plan         |
| `31b112d` | docs: spec — operator vocabulary + musician-language banners (hide internals)    |

The spec is at
`docs/superpowers/specs/2026-05-16-shared-placement-design.md` (now
~594 lines after the vocabulary revision). Decisions locked during
the brainstorm:

1. **Wrapper Constituent** as the placement carrier (rather than a
   tuple layer or a `Constituent` rewrite). Each placement is a thin
   Phrase Constituent (`PhraseMetadata::role == "placement"`) whose
   first child is the shared canonical Phrase (literally the same
   `shared_ptr`) and whose remaining children are overlay Loops.
2. **Shared-by-default capture; long-press Mark In = overlay.**
   Long-press is the cross-platform analog of an Option-modifier
   (works identically on iOS and desktop, no chord required).
3. **Fork is irreversible.** UndoStack covers immediate regret; no
   re-share/merge-back command.
4. **Tie-bar render** across shared Pills; small dot for overlays;
   prime mark for forked placements.
5. **Promote demo verse to ×3** + **lazy wrapping** (only wrap when
   a Phrase is placed more than once). Demo grows from 12 → 24 whole
   notes.
6. **Wrapper recognized by convention** (`isPlacementWrapper`
   predicate); no new field on `Constituent`.
7. **Wrapper overlay slots accept Loops only.** Phrases-inside-Phrases
   stays fully supported everywhere else.
8. **Runtime guard becomes pointer-aware:** duplicate ids allowed iff
   every occurrence is the same `shared_ptr`. Replaces
   `enforceSingleInstance` from the capture-promotion design.
9. **`arrangement::sequenceShared(parent, phrase, offsets, IdAllocator)`**
   — new primitive, uses the same allocator pattern as
   `promotion::promote`.
10. **Hide internals from the musician.** Operator-facing UI shows
    phrases and loops only; never tapes, never data-model vocabulary,
    never internal terms like "wrapper" / "placement" / "fork." The
    operator-facing menu label for the fork gesture is **"Vary this
    one."** Banner copy is plain musician language with no tape
    numbers, no durations, no mode indicators. Spec §15 ("Operator
    vocabulary") is the QA checklist — every new UI string in the
    implementation plan is reviewed against it. Saved as
    auto-memory `feedback-hide-internals-from-musician` so future
    sessions apply the rule automatically.

**Also done this session:**
- `todo.md:42` (the original shared-placement brief) is marked
  **SUPERSEDED** with a pointer to the spec.
- New auto-memory entry: `feedback-hide-internals-from-musician.md`.
- Spec §15 added: operator vocabulary glossary mapping every internal
  term to its musician-facing equivalent (or to "not surfaced").

---

## 2. This session's pick — write the implementation plan

**Single goal:** invoke the `superpowers:writing-plans` skill to
produce `docs/superpowers/plans/2026-05-16-shared-placement.md`,
mirroring the structure of
`docs/superpowers/plans/2026-05-15-capture-promotion.md`.

### Suggested session shape

1. Read the spec end-to-end. Especially §1 (data shape, wrapper
   invariants), §5 (promotion changes), §7 (selector changes), and
   the "Dependencies between work items" block — that block is the
   skeleton of the plan's task sequence.
2. Read the prior plan
   (`docs/superpowers/plans/2026-05-15-capture-promotion.md`) to
   mirror its TDD-task format, file-path precision, and commit
   cadence. The prior plan landed across 17 commits; the new one
   should target similar grain.
3. Invoke `superpowers:writing-plans`. The skill produces bite-sized
   tasks; each task should be 2–5 minutes of work for a competent
   engineer with zero project context. Every task must have exact
   file paths, complete code, exact commands, expected output.

   **Every task that introduces a UI string (banner copy, menu label,
   tooltip, error toast) must check the string against spec §15
   "Operator vocabulary."** No tape numbers, no data-model terms, no
   internal vocabulary in the default flow. The fork-gesture label
   is `"Vary this one"` (not "Fork"). Banner copy is
   `"Added to verse"` / `"Added to verse 2 only"` / `"New phrase
   captured"` / `"Added to verse — no section here yet"` (see §11).
   The plan's review checklist for any new string: *would a musician
   understand it without learning Sirius's data model?* If no, the
   string fails.
4. The plan's natural task sequence follows the spec's dependency
   block:

   ```
   1. arrangement::sequenceShared + tests        ← independent
   2. isPlacementWrapper predicate               ← lands with (1)
   3. Pointer-aware runtime guard + tests        ← independent
   4. promotion AttachmentMode + tests           ← needs (2), (3)
   5. TimelineViewState wrapper-aware selector   ← needs (2)
   6. TimelineView renderer (tie-bar, dot, prime) ← needs (5)
   7. DemoSession ×3 migration + tests           ← needs (1), (2), (3)
   8. MainComponent long-press + banner copy     ← needs (4)
   9. Fork gesture (data + minimal UI surface)   ← needs (5), (6)
   10. User guide Roadmap update                 ← lands with (9)
   11. todo.md SUPERSEDED                        ← already done this session
   ```

   Item 11 is already complete and should not appear in the new plan.
   Items 1 + 3 are good first parallel commits — they don't depend on
   each other.

5. After the plan is written, commit + push (single-line:
   `docs: plan — shared-placement implementation`).
6. **Then defer-to-own-session again:** the actual implementation is
   a third session. The plan-writing session ends after the plan
   ships.

### Likely "open items the implementation plan will revisit"

The spec calls these out explicitly (its last section); the plan will
need to make concrete decisions on:

- **Long-press duration tuning.** Spec proposes 500 ms; the plan
  decides whether to expose this as a JUCE configurable or hard-code.
- **Fork UI surface.** Spec pins "Preparation-mode only" but defers
  the concrete affordance. Plan picks one (right-click menu, long-
  press on Pill, or context affordance) and writes the JUCE wiring.
- **Banner copy details.** Spec lists four message templates; plan
  freezes the exact strings.

---

## 3. Build + test state (current head: `81afadd`)

Spec-only change — no code touched, no tests added or moved. Build
state is identical to the prior session:

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests            # expect: 235 / 235, 4145 assertions
```

No `.app` regen needed for the plan-writing session — the plan is
documentation.

---

## 4. Architectural ground truth carried into implementation

These are the invariants the spec relies on; the plan's tasks must
preserve them. Repeat them here so the next session doesn't have to
re-derive them from the spec.

| Invariant | Source | Notes for the plan |
|---|---|---|
| Loops are leaves with `TapeReference`; Phrases are containers with `PhraseMetadata`. No hybrid Constituents. | Pick A's `[demoSession][shape]` test + tightened `findHostRecursive`. | Wrappers are Phrases (not hybrids). Overlay Loops are leaves (not hybrids). The plan's tests pin both. |
| Constituents are immutable; every edit is copy-on-write with shared subtrees. | `Constituent.h` (lines 34-44 docstring). | Shared-placement is "use the same ChildPtr in N parents." The plan's `sequenceShared` task is literally one `shared_ptr` placed in N wrappers. |
| `ConstituentId` is project-wide. Today, unique within a tree (guard). | `Promotion.cpp` guard. | The new plan **breaks** uniqueness on purpose — duplicate ids are *legal when reached via the same `shared_ptr`* and *illegal otherwise*. Plan's pointer-aware-guard task implements this. |
| The M3 simplification (1:1 conceptual ↔ LMC, no tempo map yet) is documented in `Promotion.h/.cpp`. | Pick A preserved it. | Pick B preserves it. Shared placements compose through TempoMaps the same way as bare ones. |
| Promotion result carries `hostPhraseName` so the banner can show it. | Pick A preserved it. | New `resolvedMode` + `overlayPlacementIndex` fields extend this; banner copy gets richer (4 templates). |
| iOS is a first-class target. Sirius runs on macOS and iOS. | Project memory `project_sirius_branding_and_otto.md`. | The plan must not assume a desktop modifier exists. Long-press is the cross-platform gesture for overlay capture (spec §6). |

---

## 5. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded).
- This file (`continue.md`) — session state.
- `todo.md` — deferred items register. The shared-placement entry is
  now marked **SUPERSEDED**; the spec is the source of truth.
- `docs/superpowers/specs/2026-05-16-shared-placement-design.md` —
  **the spec the next session writes a plan for.**
- `docs/superpowers/specs/2026-05-15-capture-promotion-design.md` —
  prior spec. The new design's runtime guard replaces this one's.
- `docs/superpowers/plans/2026-05-15-capture-promotion.md` — prior
  plan. Mirror its structure when writing the new one.
- `docs/Sirius Looper Whitepaper V2.md` — conceptual model.
- `docs/Sirius Looper User Guide.md` — operator-facing how-to. Add a
  Roadmap-section line about repeating sections when item (10) of the
  spec's dependency block lands (not in the plan-writing session
  itself, but as the plan's last task).

### Project memory files (auto-loaded)

- `feedback_clean_builds.md` — always `rm -rf build` before GUI
  testing.
- `feedback_arm_disarm_is_required.md` — performer-facing arm/disarm
  gesture is mandatory.
- `feedback_defer_big_design_to_own_session.md` — when a major new
  design topic surfaces mid-session, write a comprehensive `todo.md`
  entry and stay on the current path. **The reason this session
  exists** (plan-writing is its own session; implementation is its
  own session after that).
- `feedback_claude_commits_and_pushes_master.md` — Claude commits and
  pushes to master. No PRs, no force-push. `bu.sh` is local-backup
  only and Claude doesn't run it.
- `feedback_hide_internals_from_musician.md` — **new this session.**
  Operator UI shows phrases and loops only; no tapes, no data-model
  vocabulary in default flow; advanced surfaces are opt-in. Spec §15
  is the QA checklist for every new UI string.
- `project_sirius_branding_and_otto.md` — sister apps with shared
  visual identity (deferred to its own session).
- `project_user_guide_alongside_whitepaper.md` — user guide doc lives
  in `docs/`, paired with the white paper.
