# Session Continuation — 2026-05-16 (shared-placement milestone shipped end-to-end)

> **For a fresh chat picking this up cold:** read this whole file
> before doing anything. The user's `~/.claude/CLAUDE.md` and the
> project's auto-memory (`MEMORY.md` + `*.md` in the memory dir) are
> loaded automatically and contain the rules. This file is the
> *state*: what just shipped, what's verified, and what to start on
> next.

---

## 0. Headline

**Shared-placement milestone is complete.** The verse plays three
times in the demo, all three placements share one underlying Phrase
via pointer-identity, the operator can long-press Mark In to attach
an overlay to a single placement only, and can right-click `"Vary
this one"` to fork a placement off its shared lineage. Tie-bar,
overlay-dot, and prime-mark visuals on the timeline make the three
states glanceable without text. Every user-facing string passes
spec §15 musician-vocabulary review.

Sessions A + B + C, ten tasks of the plan
`docs/superpowers/plans/2026-05-16-shared-placement.md`, **all
shipped and operator-verified end-to-end.**

---

## 1. Quick orientation

**Sirius Looper** is a real-time looping / arrangement application
for musicians, built in JUCE/C++20 with a strict separation between
a JUCE-free conceptual-time core (`core/`) and the audio/UI layers
(`engine/`, `ui/`, `app/`). Sister app to **OTTO**.

Authoritative reading, in order:

1. **`/Users/larryseyer/.claude/CLAUDE.md`** — global engineering
   rules (auto-loaded).
2. **`todo.md`** — deferred-work register. Several new entries from
   this milestone's code reviews — see §3 below.
3. **`docs/superpowers/plans/2026-05-16-shared-placement.md`** — the
   plan that just shipped. All 10 tasks are now `[x]`. The plan's
   Self-Review section is at the bottom.
4. **`docs/superpowers/specs/2026-05-16-shared-placement-design.md`**
   — the spec the plan implemented. §15 (musician-language QA
   checklist) is the bar every UI string was reviewed against.
5. **`docs/superpowers/plans/2026-05-15-capture-promotion.md`** — the
   prior plan; structural template still useful for the next
   milestone.
6. **`docs/Sirius Looper Whitepaper V2.md`** — conceptual model.
   Parts VII (Constituent abstraction) and IX (arrangement) are
   load-bearing background.
7. **`docs/Sirius Looper User Guide.md`** — operator-facing how-to.
   Task 9 added a Roadmap line about "Repeating song sections"; the
   full chapter is deferred until the gestures get long-form real-
   use testing.

**Project policies that override defaults** (full text in CLAUDE.md
and auto-memory):

- **Work directly on `master`.** No feature branches unless asked.
- **Commits AND pushes are authorized.** Claude commits + pushes to
  `origin/master` as deliverables land. No PRs, no `--force`, no
  `--no-verify`. See memory `feedback-claude-commits-and-pushes-master`.
  `bash/bu.sh` is the user's *local* backup tool — Claude doesn't run
  it.
- **Single-line commit messages**, format `<type>: <short title>`.
  No Co-Authored-By trailer.
- **Never run `open ...` or any GUI-launching command.** Operator-
  side `.app` verification is the user's job.
- **Hide internals from the musician.** Every new UI string is
  checked against spec §15.

---

## 2. What just shipped — Session C, 2026-05-16

Five commits on master, all pushed to `origin/master`:

| SHA       | Subject                                                                                       |
|-----------|-----------------------------------------------------------------------------------------------|
| `dd1c28c` | feat: MainComponent — long-press Mark In requests Overlay; banner uses §11 musician copy      |
| `38667d0` | feat: fork — 'Vary this one' context menu on placement Pills                                  |
| `6429029` | docs: todo.md — log Task 7 value_or→jassert + Task 8 refreshAll() follow-ups                  |
| `138b35b` | docs: user guide — Roadmap line for repeating song sections                                   |
| (Task 10) | (Task 10 commits are this handoff + the todo.md IMPLEMENTED flip; see below)                  |

**Full milestone arc** (Sessions A + B + C, 8 feature commits + 3
docs commits):

| SHA       | Session | Subject                                                                                       |
|-----------|---------|-----------------------------------------------------------------------------------------------|
| `936582f` | A       | feat: arrangement — sequenceShared + isPlacementWrapper predicate                             |
| `d8a2479` | A       | feat: promotion — AttachmentMode + pointer-aware guard + wrapper-aware host walk              |
| `b59a76e` | A       | docs: promotion — justify promote() length per CLAUDE.md function-size rule                   |
| `f309e2c` | B       | feat: TimelineViewState — wrapper-aware Pills with sharedSiblings/overlays/forked             |
| `503bab0` | B       | feat: TimelineView — tie-bar across shared placements + overlay dot + fork prime              |
| `74d0463` | B       | feat: DemoSession — verse plays three times via shared placement                              |
| `dd1c28c` | C       | feat: MainComponent — long-press Mark In requests Overlay; banner uses §11 musician copy      |
| `38667d0` | C       | feat: fork — 'Vary this one' context menu on placement Pills                                  |
| `138b35b` | C       | docs: user guide — Roadmap line for repeating song sections                                   |

**Test suite:** 235 / 4145 → **250 / 4269** assertions, all green.

**Operator gates passed:** Task 5 (tie-bar no-regression), Task 7
(four §11 banner scenarios), Task 8 (five-step fork gesture script),
Task 10 (end-to-end milestone walkthrough).

---

## 3. Known follow-ups (from milestone code reviews — in `todo.md`)

Three review-surfaced cleanups, none blocking, all individually small:

1. **Hoist Shared-path splice out of `promote()`** (Session A
   review). Extract the pointer-identity-preserving Shared splice
   into a private anonymous-namespace helper; drops `promote()` from
   ~226 to ~165 lines. Surfaced 2026-05-16, commit `b59a76e`-era.

2. **`announceCapture` Overlay branch: `value_or` → `jassert`**
   (Session C Task 7 review). Replace defensive `value_or`
   fallbacks with `jassert` so debug builds catch contract
   regressions loudly rather than rendering silently-wrong banner
   copy. Surfaced 2026-05-16, commit `dd1c28c`.

3. **Extract `MainComponent::refreshAll()`** (Session C Task 8
   review). The four-call refresh sequence is duplicated at five
   sites; collapse to one helper. Surfaced 2026-05-16, commit
   `38667d0`.

Plus the pre-existing items already in `todo.md`:

- **2026-05-15 — DemoSession intro/outro Phrase-vs-Loop convention**
  (intro/outro are hybrids; the milestone's verse uses the strict
  Phrase-shell-with-Loop-child shape).
- **2026-05-15 — Session directory format** (Whitepaper V2 §7.8).
- **2026-05-15 — Load dialog macOS TCC issue.**
- **2026-05-15 — OTTO Look-and-Feel integration**.
- **2026-05-15 — Marketing site asset gaps.**
- **2026-05-15 — M5 plugin scanner redesign.**
- Various M2/M3/M5/M6/M7/M8 operator-verification deferrals.

---

## 4. Next milestone — candidates

The shared-placement milestone closes a major architectural arc.
The next big-topic candidates, in roughly priority order:

1. **Persistence: session-format encoding of sharing.** Today the
   on-disk session.json shape (Whitepaper V2 §7.8) doesn't account
   for placement wrappers or the shared `ChildPtr` invariant. A
   session with verse × 3 would serialize the verse Phrase three
   times and lose pointer-identity on reload. Needs:
   - A serialization scheme that emits each Phrase once and
     references it from wrappers by id.
   - A deserialization step that re-establishes the
     `enforceSharedInstancesAreShared` invariant before promote()
     ever runs.
   - The directory-format work already deferred in `todo.md` is the
     natural place to land this — they're coupled.

2. **OTTO L&F integration.** Big, well-scoped, sister-app-aligned.
   Has a four-option design choice already enumerated in `todo.md`
   (shared-submodule decided; module location TBD between
   new-top-level-repo / OTTO-canonical / Sirius-canonical / vendor-
   first).

3. **The full "Repeating song sections" user-guide chapter.** Task 9
   added a Roadmap bullet; the full chapter lands once the gestures
   have been used in real performance and the language stabilises.
   Needs operator time-on-instrument, not implementation time.

4. **Spec §16 open items** (frozen during this milestone but worth
   revisiting):
   - Desktop accelerator: Option-click on Mark In for overlay,
     parallel to the long-press touch gesture.
   - Per-instance metadata beyond overlay Loops (per-placement
     entrance/exit characters? Per-placement automation?).
   - Phrase-shaped overlays (today overlays are Loops only).

5. **M5 plugin scanner crash + redesign.** Lower priority unless
   the user's plugin folder grows again.

6. **The three code-quality follow-ups in §3.** Bundleable as one
   focused refactor commit; or distribute across the next milestone
   as touched-while-passing-by opportunities (favouring the
   `refreshAll()` extraction, since the next milestone will almost
   certainly add a sixth refresh-quartet site).

---

## 5. Architectural ground truth (now enforced end-to-end)

| Invariant | Source | Status |
|---|---|---|
| Loops are leaves with `TapeReference`; Phrases are containers with `PhraseMetadata`. No hybrid Constituents (except intro/outro, tracked). | `[demoSession][shape]` test + `findHostRecursive`. | Enforced for verse/wrappers; intro/outro still hybrid (tracked). |
| Wrappers are Phrases (role `"placement"`). Forked wrappers are Phrases (role `"forked-placement"`). Overlay Loops are leaves. | Spec §1, Task 1 predicate. | Enforced — `isPlacementWrapper`, `sequenceShared`, deep-copy with fresh ids. |
| Constituents are immutable; every edit is copy-on-write with shared subtrees. | `Constituent.h` docstring. | Preserved through promote() (pointer-identity-preserving splice) and fork (deep-copy of shared subtree). |
| `ConstituentId` duplicates are legal iff via the same `shared_ptr`, illegal otherwise. | Spec §3, Task 2 guard. | Enforced at runtime — `enforceSharedInstancesAreShared`. |
| M3 simplification (1:1 conceptual ↔ LMC, no tempo map inverse yet). | Carry-over. | Preserved through every Session A/B/C splice. |
| Promotion result carries `hostPhraseName`, `resolvedMode`, `overlayPlacementIndex`, `mintedPhraseId`. | Task 3. | Live — banner reads all four via §11 templates (Task 7). |
| iOS is a first-class target. | Project memory. | Long-press gesture (touch-friendly) chosen over desktop-modifier-click. |
| Operator vocabulary rule §15. | Spec §15. | Enforced via greps + line-by-line review at every UI-touching task (4-9); end-to-end verified at Task 10. |
| TimelineView Pills emit one-per-wrapper for placement and forked wrappers. | Task 4. | Live, with pointer-identity grouping for sharedSiblings. |
| Tie-bar grouping stable across paint frames via min-id key. | Task 5. | Explicit comment at the key-derivation site. |
| Demo: 24 whole notes, verse × 3 via shared Phrase id 20. | Task 6. | Pinned by `[demoSession][shape]` and `[demoSession][shared]`. |
| Long-press Mark In ≥ 500 ms = Overlay request; release resets. | Task 7. | `kOverlayLongPressMs = 500`; lambda timer; orange tint confirms upgrade. |
| Fork gesture: right-click / Ctrl-click / touch-long-press a Pill → `"Vary this one"` menu → deep-copy with fresh ids + role flip. | Task 8. | Wired through `TimelineView::onPillContextMenuRequested`. |

---

## 6. Build + test state

```bash
cd "/Users/larryseyer/Sirius Looper"
cmake --build build --target SiriusTests        # incremental
./build/tests/SiriusTests                       # expect 250 / 4269
```

The `.app` is fresh from Task 10's clean Release rebuild at
`build/app/SiriusLooper_artefacts/Release/Sirius\ Looper.app`. No
warnings from any Sirius source (only the usual JUCE/Catch2
upstream noise).

---

## 7. Authoritative references

- `~/.claude/CLAUDE.md` — global rules (auto-loaded).
- `~/.claude/projects/-Users-larryseyer-Sirius-Looper/memory/MEMORY.md`
  — auto-memory index (auto-loaded).
- This file (`continue.md`) — session state.
- `todo.md` — deferred items register. Shared-placement entry now
  marked SUPERSEDED-AND-IMPLEMENTED 2026-05-16. Three new
  code-review-surfaced entries from this milestone.
- `docs/superpowers/plans/2026-05-16-shared-placement.md` — the
  shipped plan. All 10 tasks done; Self-Review at the bottom.
- `docs/superpowers/specs/2026-05-16-shared-placement-design.md` —
  the spec, fully implemented.
- `docs/superpowers/plans/2026-05-15-capture-promotion.md` — prior
  plan; still the structural template for future plans.
- `docs/Sirius Looper Whitepaper V2.md` — conceptual model.
- `docs/Sirius Looper User Guide.md` — operator-facing how-to.
  Roadmap bullet on "Repeating song sections" updated this session;
  full chapter deferred.

### Project memory files (auto-loaded)

- `feedback_clean_builds.md` — always `rm -rf build` before GUI
  testing.
- `feedback_arm_disarm_is_required.md` — performer-facing
  arm/disarm gesture is mandatory.
- `feedback_defer_big_design_to_own_session.md` — when a major new
  design topic surfaces mid-session, write a comprehensive `todo.md`
  entry and stay on the current path. Applied multiple times in
  Sessions A–C.
- `feedback_claude_commits_and_pushes_master.md` — Claude commits
  and pushes to master. No PRs, no force-push.
- `feedback_hide_internals_from_musician.md` — Spec §15 is the QA
  checklist. Enforced end-to-end this milestone.
- `project_sirius_branding_and_otto.md` — sister apps with shared
  visual identity (deferred to its own session).
- `project_user_guide_alongside_whitepaper.md` — user guide doc
  lives in `docs/`, paired with the white paper. Roadmap updated;
  full chapter deferred per the policy of "land the feature, write
  the chapter once real use confirms the language."
