# Session Continuation — 2026-05-16 (DemoSession reconciled; open Pick B brainstorm)

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
2. **`docs/Sirius Looper Whitepaper V2.md`** — conceptual model. Part
   VII (Constituent unifying abstraction) and Part IX (arrangement) are
   the load-bearing background for this session's pick.
3. **`docs/Sirius Looper User Guide.md`** — operator-facing how-to.
   Chapter 1 ("Capturing Phrases and Loops") covers what the prior
   session shipped.
4. **`docs/superpowers/specs/2026-05-15-capture-promotion-design.md`**
   — capture promotion design. **§3** documents the multi-instance
   runtime guard that *this* session's pick will eventually remove.
5. **`todo.md`** — deferred-work register. The shared-placement entry
   at line 42 is the brief for the work this session opens.

**Project policies that override defaults** (full text in CLAUDE.md
and in the auto-memory):

- **Work directly on `master`.** No feature branches unless asked.
- **Commits AND pushes are authorized.** New as of 2026-05-16 —
  Claude commits to master and pushes to `origin/master` as
  deliverables land. No PRs (trunk push only). No `--force`. No
  `--no-verify`. See memory `feedback-claude-commits-and-pushes-master`.
  `bash/bu.sh` is the user's *local* backup tool (it derives a Dropbox
  zip filename from the last commit message — that's why commits stay
  single-line) — Claude does not run it.
- **Single-line commit messages**, format `<type>: <short title>`. No
  Co-Authored-By trailer.
- **Never run `open ...` or any GUI-launching command.** Operator-side
  .app verification is something the *user* runs at their terminal.
- **Defer big design topics to their own session** rather than letting
  them bloat the current one. See memory
  `feedback-defer-big-design-to-own-session`. This session is
  *itself* an instance of that policy — the prior session deferred
  shared-placement architecture into a dedicated brainstorm, which is
  what this session opens.

---

## 1. What just shipped (last session, 2026-05-16)

**Pick A — DemoSession Phrase-vs-Loop reconciliation.** Single commit
on master, pushed to `origin/master`:

| SHA       | Subject                                                                                  |
|-----------|------------------------------------------------------------------------------------------|
| `001f314` | refactor: DemoSession + Promotion — eliminate Phrase+Loop hybrids; tighten host predicate |

The intro (id 10) and outro (id 30) used to be **hybrid leaves**
carrying both `PhraseMetadata` and `TapeReference` on a single
Constituent. They now mirror the verse's shape: Phrase shell +
single Loop child (ids 11, 31). `Promotion::findHostRecursive` is
now `isPhrase() && !tapeReference().has_value()` so a hybrid is
rejected as a host even if one is ever reintroduced — defense in
depth.

Test count: **235 / 235** pass, 4145 assertions (was 233 / 4124).
New cases: `[demoSession][shape]` locks the demo's structural
invariant; `[promotion][host][hybrid-rejection]` locks the tightened
predicate.

**Build-graph fix bundled in the same commit:** `app/DemoSession.cpp`
moved from the `SiriusLooper` (JUCE app) target into `SiriusAppCore`
(JUCE-free static lib) so unit tests can link it. DemoSession was
already JUCE-free; only the target placement was wrong.

**Policy change bundled in the same session:** the
"GitHub-handled-in-a-separate-terminal" memory was replaced with
`feedback-claude-commits-and-pushes-master`. Claude now handles both
sides of the local→remote flow on this repo.

---

## 2. This session's pick — Pick B: shared-placement architecture

The brainstorm-deferred big topic. The brief lives in `todo.md:42`.

### The problem

Today, `arrangement::sequence` (`core/src/Arrangement.cpp:60`,
`placedAt`) creates **per-placement Constituent copies**. When the
operator says "verse plays 3 times," the data model holds 3 separate
`Constituent` objects that happen to share the same `ConstituentId`.
The capture-promotion runtime guard (`promotion::promote`, throws
`std::logic_error` if a `ConstituentId` appears more than once) is the
write-protect that catches this today — its existence is a *receipt*
that the data model and the operator's mental model disagree.

The operator's musical model: **"the verse plays 3 times, sharing
common layers (drums, bass, rhythm) but with per-instance vocal
variations."** Repeated phrases should be **shared by reference** with
**per-instance overlay buckets** for the differentiating layers.

### Why this is the right next pick

- It is the largest single unblocking for M8 (ensemble / multi-tape
  capture) and the proper completion of M3's arrangement story.
- The capture-promotion runtime guard is the *only* thing keeping
  multi-instance correctness today. As soon as the operator wants
  verse × 3 in the demo session, the guard fires. The guard is the
  IOU; this work is paying it.
- It is bounded enough to be a focused multi-session effort:
  brainstorm → spec → plan → execute, the same loop that produced
  capture-promotion.

### The six steps (verbatim from `todo.md:68-86`)

1. **Settle Path B from the brainstorm:** the arrangement layer
   becomes a sequence of `(Phrase ChildPtr, Position, optional
   overlay-children)` tuples. The Phrase ChildPtr is shared across
   placements.
2. **Design the per-instance overlay UX** — where overlays attach in
   the data model, how the timeline distinguishes shared vs overlay
   rendering, whether overlays are themselves Phrase-shaped or a new
   struct.
3. **Design the "fork this placement into its own Phrase" gesture** —
   the escape hatch for the rare "this verse is special" case (e.g.
   the operator wants verse 3 to permanently diverge — drum fill,
   re-harm, different feel).
4. **Decide undo semantics across instances:** one undo entry =
   all-instances revert, or per-instance? Both have arguments —
   operator-visible decision.
5. **Extend `promotion::promote` to handle the multi-instance case** —
   remove the runtime guard at `core/src/Promotion.cpp` (the one that
   throws on duplicate ids), propagate Loop adds across all
   Constituents matching the host id, handle overlay vs shared
   attachment based on operator gesture.
6. **Update `selectTimelineView`** to render shared vs forked
   placements distinguishably (visual cue — maybe a tie-bar across
   shared instances, or a per-instance number badge).

### Suggested session shape

Open with **brainstorming** (the
`superpowers:brainstorming` skill — the same one that produced
capture-promotion). The brainstorm output should be a design spec at
`docs/superpowers/specs/2026-05-16-shared-placement-design.md`
covering at minimum:

- The new arrangement data shape (Path B from step 1 above; sketch
  the C++ type signature).
- Where overlays attach (step 2). Mock data shape for one verse
  with one shared layer and one per-instance overlay.
- The fork gesture (step 3). Where it lives in the UI; what it does
  to the data; whether it's reversible (probably no — explicit
  divergence intent).
- The undo decision (step 4). Pick one with stated rationale —
  later sessions can revisit if the operator dislikes it in
  practice, but the spec needs a single answer to plan against.
- A migration story for the demo session — verse currently plays
  once; the demo can stay single-instance, or the spec can promote
  it to verse × 3 as a built-in proof of the new shape.

After the spec lands and the user approves, the next session writes
an implementation plan at
`docs/superpowers/plans/2026-05-16-shared-placement.md` (mirror the
structure of `docs/superpowers/plans/2026-05-15-capture-promotion.md`
which delivered the prior pick across 17 commits).

### Likely scope of the *first* implementation commit

The brainstorm shape determines this, but a reasonable wager is:

- Extend `Arrangement.h` / `Arrangement.cpp` with the new shared-
  placement layer struct (without yet touching call sites).
- Add tests that lock the new struct's behavior under copy-on-write
  and id-sharing.
- *Do not* touch `Promotion.cpp`'s guard yet — it stays as the
  load-bearing safety net until step 5.

Then subsequent commits unwind the per-placement copies in
`arrangement::sequence`, teach `TimelineViewState` the new shape,
land the overlay UX, land the fork gesture, and finally tear out the
promotion guard.

### Files most likely to change (from `todo.md`)

- `core/include/sirius/Arrangement.h`
- `core/src/Arrangement.cpp`
- `core/include/sirius/Constituent.h` (possibly — depends on whether
  overlay buckets attach to Constituent or live on the layer struct)
- `ui/src/TimelineViewState.cpp` (rendering distinction)
- `ui/include/sirius/UndoStack.h` (if step-4 undo semantics need a
  new entry type)
- `core/src/Promotion.cpp` (the guard removal, step 5; far in the
  future)
- `docs/Sirius Looper User Guide.md` (Roadmap section now; new
  chapter once the feature lands)

---

## 3. Build + test state (current head: `001f314`)

```bash
cd "/Users/larryseyer/Sirius Looper"
rm -rf build && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tests/SiriusTests            # expect: 235 / 235, 4145 assertions
./build/tests/SiriusTests "[promotion]"     # 8 cases (was 7)
./build/tests/SiriusTests "[demoSession]"   # 1 case (new)
```

`.app` bundle is at `build/app/SiriusLooper_artefacts/Release/Sirius Looper.app`.
Operator-side verification of the Pick A change was completed by the
user this session — the intro/outro Pills render unchanged and the
host-capture banner correctly names intro/outro as Phrase hosts.

For Pick B specifically, expect *frequent* unit-test-only iterations
during the brainstorm and the first implementation commits — there's
no operator-visible UI change until step 6, and arrangement-layer
changes are the kind of thing the headless test harness should catch
fully before any .app exercise.

---

## 4. Architectural ground truth carried into Pick B

These are invariants the prior sessions established. The Pick B
design must respect or explicitly relax each one.

| Invariant | Source | Notes for Pick B |
|---|---|---|
| Loops are leaves with `TapeReference`; Phrases are containers with `PhraseMetadata`. No hybrid Constituents. | This session's `[demoSession][shape]` test + tightened `findHostRecursive`. | Overlay buckets must not reintroduce hybrids. If overlays live on a Constituent, that Constituent must still be one or the other, not both. |
| Constituents are immutable; every edit is copy-on-write with shared subtrees. | `Constituent.h` (lines 34-44 docstring). | Sharing a Phrase ChildPtr across placements *is* already idiomatic — the prior shape just didn't surface that as an explicit construct. Path B is "make it the primary construct," not "introduce sharing." |
| `ConstituentId` is project-wide and currently expected to be unique within a tree (enforced today by `promotion::promote`). | `Promotion.cpp` guard. | Pick B explicitly **breaks** this. The new model is "shared-by-reference implies shared id across placements is *correct*, not an error." The guard goes away in step 5. |
| The M3 simplification (1:1 conceptual ↔ LMC, no tempo map yet) is documented in `Promotion.h/.cpp`. | This session preserved it. | Pick B should not couple itself to this — when tempo maps land, the shared-placement design must still hold. |
| The promotion result type carries `hostPhraseName` so the banner can show it. | This session preserved it. | Per-instance capture in Pick B may need a richer description ("instance 2 of verse"). Banner copy is an operator-visible API surface. |

---

## 5. Open questions to surface in the brainstorm

These are the questions that the prior brainstorm punted on — the
`todo.md` entry already named most. List them up front so the
brainstorm doesn't redo the discovery.

1. **Overlay attachment point.** Do overlays attach to (a) the
   shared Phrase ChildPtr (impossible — it's shared, mutating it
   would affect all instances), (b) the layer-struct tuple (per
   placement), or (c) a new per-instance Constituent that wraps the
   shared Phrase? Each has different undo and rendering implications.
2. **Fork irreversibility.** "Fork this placement into its own
   Phrase" — is it reversible? Probably no (explicit divergence
   intent), but the spec must say.
3. **Undo grain.** Per-instance vs all-instances. Currently undo is
   per-edit (`UndoStack::Entry`), and a multi-instance edit produces
   one entry. The natural semantics are "all-instances revert" but
   the operator may want per-instance for surgical undo of an
   overlay add.
4. **Timeline rendering.** Should three shared placements of a verse
   render as one tied-together visual group with a "× 3" badge, or
   three side-by-side Pills with a subtle connecting line? The
   brainstorm should produce a sketch.
5. **Capture into a shared placement.** When the operator captures
   into instance 2 of a shared verse: does the new Loop become
   (a) a shared layer added to the underlying Phrase (visible in all
   three instances), or (b) an overlay attached to instance 2 only?
   Operator gesture must distinguish these. Probably the default
   should be "shared" (the common case) with a modifier-held gesture
   for "this instance only."
6. **Demo session migration.** Does the spec promote the demo verse
   to verse × 3 to exercise the new shape natively, or stay
   single-instance with new tests covering the shared case
   synthetically? Recommend promoting it — the demo is the canonical
   reference operators look at first.

---

## 6. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded). New entry this session:
  `feedback-claude-commits-and-pushes-master`.
- This file (`continue.md`) — session state.
- `todo.md` — deferred items register. The shared-placement entry at
  line 42 is this session's primary brief.
- `docs/Sirius Looper Whitepaper V2.md` — conceptual model. Parts
  VII–IX are most relevant.
- `docs/Sirius Looper User Guide.md` — operator-facing how-to.
- `docs/superpowers/specs/2026-05-15-capture-promotion-design.md` —
  prior session's spec. §3 documents the runtime guard that this
  session's pick will eventually remove. Mirror the structure when
  writing the new spec.
- `docs/superpowers/plans/2026-05-15-capture-promotion.md` — prior
  session's plan (10 tasks, all complete). Mirror the structure
  when writing the new plan.

### Project memory files (auto-loaded)

- `feedback_clean_builds.md` — always `rm -rf build` before GUI
  testing.
- `feedback_arm_disarm_is_required.md` — performer-facing arm/disarm
  gesture is mandatory.
- `feedback_defer_big_design_to_own_session.md` — when a major new
  design topic surfaces mid-session, write a comprehensive `todo.md`
  entry and stay on the current path. **The reason this session
  exists.**
- `feedback_claude_commits_and_pushes_master.md` — Claude commits
  to master and pushes to `origin/master`. No PRs, no force-push.
  `bu.sh` is local-backup only and Claude doesn't run it. **New
  this session.**
- `project_sirius_branding_and_otto.md` — sister apps with shared
  visual identity (deferred to its own session).
- `project_user_guide_alongside_whitepaper.md` — user guide doc
  lives in `docs/`, paired with the white paper.
