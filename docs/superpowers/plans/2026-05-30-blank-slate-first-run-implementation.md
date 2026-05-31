# Blank-Slate First-Run + Phrase/Loop Creation — Implementation Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) to implement this plan slice-by-slice. **This is a phased roadmap across many subsystems.** Slice 1 is fully detailed in bite-sized TDD steps. Slices 2–8 are scoped (goal / spec refs / files / interfaces / tests / done-when / deps); **write a focused detailed plan for each slice at the start of its execution**, with fresh context on that subsystem's live files. Each slice produces working, testable software on its own.

**Goal:** Replace IDA's demo-song boot with an honest first-run flow: blank slate → create a channel and pick its input → record → a phrase appears and plays → undo/redo — with tapes recording only while assigned, owned by a project, and triggerable from a phrase-button bank.

**Architecture:** The clock always runs; a tape records iff ≥1 input is assigned (resource-aware capture). Tapes are owned by an IDA *project* (project-scoped folders, no orphans). Phrase/loop creation is a per-phrase state machine formalized over the existing mark-in/out + `promotion::promote()` path, driven by a source-agnostic command layer (GUI today, MIDI/pedal later).

**Tech Stack:** C++/JUCE; `core` (JUCE-free), `engine`, `persistence`, `ui`, `app`; Catch2 (`IdaTests`); CMake + Ninja. Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md`.

---

## Cross-cutting rules (apply to every slice)

- **Engine logic is TDD'd headless** in `IdaTests`; **GUI wiring is operator-verified** (clean `rm -rf build` before each operator hand-off, per `[[feedback_clean_builds_only_for_testing]]`).
- **RT-safety:** anything reachable from the audio callback stays `noexcept`, no alloc/lock/IO (`docs/RT_SAFETY_CONTRACT.md`). The arm-gating of Slice 4 must flip recording state off the hot path.
- **Undo:** every create/clear/rename/assign gesture pushes one labeled `UndoStack` entry (infra already exists — `ui/include/ida/UndoStack.h`).
- **iOS:** every right-click is paired with long-press from the start; iOS is Release-only.
- **Commits:** each task commits; the implementing subagent pushes its task commits to `origin/master` (`[[feedback_subagents_push_to_master]]`). Single-line commit messages.
- **Destructive ops:** tape deletion only behind the deliberate-erase warning (spec §2.1) — never auto-delete audio.

## File-structure map (across the feature)

- `core/include/ida/TapePool.h` + `core/src/TapePool.cpp` — empty-allowed pool, optional primary (Slice 1).
- `persistence/.../SessionFormat.*` — empty-pool round-trip; project metadata (Slices 1–2).
- `core`/`persistence` new: an **IdaProject** unit (id/name/folder/created-timestamp) + project-scoped path + tape filename builder (Slice 2).
- `engine/include/ida/InputMixer.h` — unpin `TapeId{1}`; assignment-gated routing (Slices 1 compile-guard, 4 rework).
- `engine`/`app` capture wiring (`TapeRecordWriter` construction, `MainComponent` ~4255–4320) — recording iff assigned (Slice 4).
- `core/include/ida/CaptureSession.h` — terminology split (marking vs tape recording) (Slice 4/5).
- `core/include/ida/Promotion.h` + a new **CaptureCommand** layer — per-phrase state machine + source-agnostic commands (Slice 5).
- `app/MainComponent.cpp` — boot path (~4176), `rebuildInputStrips` (~7921), `refreshInputDestinations` (~7734), `refreshOutputMixerPhraseChannels` (~7105), New Song command (Slices 3–6).
- `app/DemoSession.{h,cpp}` — retired from boot (Slice 3).
- `ui` — Tapes tab archive + reveal (Slice 7); phrase-button bank component (Slice 8); colors via `ui/include/ida/IdaPalette.h`.

---

## Detailed slice plans (one document each)

Each slice has a full bite-sized plan produced 2026-05-31; execute each via `superpowers:subagent-driven-development`:

| Slice | Detailed plan |
|---|---|
| 1 | *(detailed inline below)* |
| 2 | `docs/superpowers/plans/2026-05-30-slice-2-ida-project-and-storage.md` |
| 3 | `docs/superpowers/plans/2026-05-30-slice-3-blank-slate-and-new-song.md` |
| 4 | `docs/superpowers/plans/2026-05-30-slice-4-channel-creation-and-arm-gating.md` |
| 5 | `docs/superpowers/plans/2026-05-30-slice-5-phrase-loop-state-machine.md` |
| 6 | `docs/superpowers/plans/2026-05-30-slice-6-play-all-loops.md` |
| 7 | `docs/superpowers/plans/2026-05-30-slice-7-tapes-tab-archive.md` |
| 8 | `docs/superpowers/plans/2026-05-30-slice-8-phrase-button-bank.md` |

## Cross-slice findings from the detailed-planning pass (must-read before executing)

The per-slice planning (live-code exploration) surfaced realities the scope-level slices missed:

- **Slice 6 = per-loop output channels (operator-locked 2026-05-31) — and a correctness fix.** The Output Mixer gets **one channel per loop** (`T#P#L#`), keyed by the **leaf-loop** ConstituentId — which is exactly what `PlaybackResolver` resolves, so it fixes the prior pill-id-vs-loop-id keying defect *by construction* (no shared-channel re-keying). A phrase's loop-channels **sum at a per-phrase bus** → master (spec §8.6). This **supersedes** the earlier "one channel per phrase, sum in `renderPlaybackStep`" plan — Slice 6 must be re-derived against §8.6 and now **does** touch `OutputMixer`/`Bus` (a per-phrase bus). The phrase-button bank (Slice 8) and "play all loops" both target that per-phrase bus.
- **Slice 4 is mostly removal.** "Records iff assigned" is *already* structural (`renderInputGraph` only delivers to touched tape slots; `TapeRecordWriter` is lazy and never deletes). The work is killing the looper-floor (`canDisarmChannelRecording`) and unpinning `TapeId{1}` — whose **real** pin is the `MixerGraph` graph-front Tape terminal, not just the `removeTape` guard.
- **The `tape-<id>.idatape` filename is hardcoded in 3 sites** (`audio/src/TapeRecordWriter.cpp:104`, `app/MainComponent.cpp:6059` and `:6081`), not one. Slice 2 builds the path/name convergence point; Slices 3/4 rewire the live writer + the two reader paths to it. **Slice 7's Reveal must target the same path source the writer uses**, or it opens an empty folder. `.idatape` extension kept (the spec named only the `tape_<x>` stem; container is FLAC/PCM by tier).
- **`primary()`-optional ripple is wider than Slice 1 listed:** also `MainComponent.cpp:8265 / 8286 / ~8861` and `mirrorTapePool` (empty-pool safety). Slice 1/3 must guard all of them or the `IDA` target won't compile.
- **Transport:** `TransportBarHost::playPauseClicked()` *toggles* (stops if playing). Record-while-stopped must call `OttoHost::play()` **unconditionally** — Slice 5 routes through an injected `ITransportControl`/adapter, not the toggle.
- **Slice 8 defines the Slice 5 seam:** `IPhraseStateSource` + `IPhraseCommandSink` in `core/`, with a HARD-STOP if Slice 5's real state machine is absent at execution (no parallel capture path, per spec §8.4). Reuses `app/StripContextOverlay.h` (right-click + 500 ms long-press + inline rename) and `selectTimelineView().pills`. Colour map: rec→`error`, play→`success`, overdub→`warning`, stopped→dim `phraseColour`, empty→`transportInactive`.
- **Channel add/remove undo (spec §15.2 "default YES") has no natural lane** — `UndoStack` is Constituent-tree-shaped, not channel-shaped. Flagged for operator sign-off rather than forced into a faked entry.

---

## Slice 1 — TapePool: legal empty pool, optional primary  *(DETAILED — start here)*

**Spec refs:** §11 (TapePool ≥1 floor relaxed), §2.2 (no-orphan is a project concern, not a pool floor).
**Depends on:** nothing.
**Done when:** `TapePool` can be empty, `primary()` is optional, `remove()` can empty the pool and no longer pins the primary, the empty pool round-trips through `SessionFormat`, and `IdaTests` + the `IDA` app target both build green.

**Files:**
- Modify: `core/include/ida/TapePool.h`
- Modify: `core/src/TapePool.cpp`
- Modify: `tests/TapePoolTests.cpp`
- Modify: `persistence/` `SessionFormat` source (the `deserializeTapePool` empty-rejection)
- Modify (compile-guard only): `engine/include/ida/InputMixer.h` / its `.cpp`, and `app/MainComponent.cpp` primary() call sites

- [ ] **Step 1: Rewrite the failing tests in `tests/TapePoolTests.cpp`**

Replace the four affected cases so they encode the *new* contract. (`primary()` now returns `std::optional<TapeId>`.)

```cpp
TEST_CASE ("default TapePool is empty (blank-slate)", "[tape-pool]")
{
    TapePool pool;
    CHECK (pool.count() == 0);
    CHECK (pool.tapes().empty());
    CHECK_FALSE (pool.primary().has_value());   // no primary when empty
    CHECK (pool.find (TapeId (1)) == nullptr);
}

TEST_CASE ("TapePool::add seeds the first tape and primary follows the front", "[tape-pool]")
{
    TapePool pool;
    const auto first = pool.add ("Tape 1");
    CHECK (first == TapeId (1));                 // ids still start at 1
    REQUIRE (pool.primary().has_value());
    CHECK (*pool.primary() == TapeId (1));
    const auto second = pool.add ("Drums");
    CHECK (second == TapeId (2));
    CHECK (*pool.primary() == TapeId (1));       // primary = front, unchanged by add
}

TEST_CASE ("TapePool::remove can empty the pool and does not pin the primary", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("A");   // id 1, becomes front/primary
    const auto b = pool.add ("B");   // id 2
    REQUIRE (pool.count() == 2);

    SECTION ("removing the primary is allowed; front advances")
    {
        CHECK (pool.remove (a));
        CHECK (pool.count() == 1);
        REQUIRE (pool.primary().has_value());
        CHECK (*pool.primary() == b);            // new front
    }
    SECTION ("removing the last tape empties the pool")
    {
        REQUIRE (pool.remove (a));
        REQUIRE (pool.remove (b));
        CHECK (pool.count() == 0);
        CHECK_FALSE (pool.primary().has_value());
    }
    SECTION ("removing an unknown id returns false")
    {
        CHECK_FALSE (pool.remove (TapeId (999)));
        CHECK (pool.count() == 2);
    }
}

TEST_CASE ("TapePool explicit-list ctor accepts an empty list", "[tape-pool]")
{
    TapePool pool (std::vector<TapeDescriptor> {});
    CHECK (pool.count() == 0);
    CHECK (pool.add ("First") == TapeId (1));    // nextId_ starts at 1 when empty
}
```

Also update the two cases that assume a seeded default tape: in *"TapePool::add appends … monotonic id"* the first `add` is now `TapeId(1)` (not `2`) and `count()` is one less; in the TAPECOLOR case *"default TapePool seeds primary tape with tapeColor == None"*, first `add ("Tape 1")` then assert `pool.at(0).tapeColor == None`. And in *"deserializeTapePool rejects empty …"*, change the empty-array case from `REQUIRE_THROWS_AS` to a round-trip that **succeeds** with `count()==0` (move it into the round-trip case below).

- [ ] **Step 2: Add the empty-pool SessionFormat round-trip test**

```cpp
TEST_CASE ("empty TapePool round-trips through SessionFormat", "[tape-pool][sessionformat]")
{
    TapePool empty (std::vector<TapeDescriptor> {});
    const auto json   = ida::persistence::serializeTapePool (empty);
    const auto loaded = ida::persistence::deserializeTapePool (json);
    CHECK (loaded.count() == 0);
}
```

- [ ] **Step 3: Run the tests, verify they FAIL**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R tape-pool`
Expected: FAIL — `primary()` doesn't return `optional`; default ctor still seeds Tape 1; empty list throws.

- [ ] **Step 4: Change `TapePool.h`**

Add `#include <optional>`. Change the signature and update the comments (the ≥1 wording is now false):

```cpp
    /// The primary tape — the first entry, or nullopt when the pool is empty
    /// (the blank-slate / New Song state).
    std::optional<TapeId> primary() const noexcept;
```
Update the class doc ("minimum one" → "possibly empty") and the ctor docs (default ctor seeds **no** tapes; explicit ctor accepts an empty list).

- [ ] **Step 5: Change `TapePool.cpp`**

```cpp
TapePool::TapePool() = default;   // empty pool; nextId_ stays 1

TapePool::TapePool (std::vector<TapeDescriptor> tapes)
    : tapes_ (std::move (tapes))
{
    std::unordered_set<std::int64_t> seen;
    std::int64_t maxId = 0;
    for (const auto& t : tapes_)
    {
        if (! seen.insert (t.id.value()).second)
            throw std::invalid_argument ("ida::TapePool: duplicate tape id");
        maxId = std::max (maxId, t.id.value());
    }
    nextId_ = maxId + 1;          // empty list -> stays 1
}

bool TapePool::remove (TapeId id)
{
    const auto it = std::find_if (tapes_.begin(), tapes_.end(),
                                  [id] (const TapeDescriptor& t) { return t.id == id; });
    if (it == tapes_.end())
        return false;
    tapes_.erase (it);
    return true;
}

std::optional<TapeId> TapePool::primary() const noexcept
{
    if (tapes_.empty()) return std::nullopt;
    return tapes_.front().id;
}
```
(Leave `add`, `rename`, `count`, `find`, `at`, `tapes` unchanged.)

- [ ] **Step 6: Relax the SessionFormat empty-rejection**

In the `persistence` `SessionFormat` source, find `deserializeTapePool` and remove the check that throws on a present-but-empty `tapes` array; construct an empty `TapePool` instead. Keep the malformed-JSON and missing-`tapes` throws. (The Step-2 test drives this.)

- [ ] **Step 7: Fix compile ripple from `optional` primary**

Run: `grep -rn "\.primary()\|->primary()" engine app ui | grep -v test`
At each call site, guard `nullopt`. The known one is `InputMixer` (pins `TapeId{1}` — `engine/include/ida/InputMixer.h:99`): where it assumed a primary, treat "no primary" as "no default tape route yet" (a node with no tape destination). **Minimal compile-safe guard only** — the full assignment-gated rework is Slice 4. Likewise guard the `MainComponent` ctor pool-seeding / `refreshInputDestinations` primary use.

- [ ] **Step 8: Build + run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "tape-pool|sessionformat"`
Expected: PASS.
Run: `cmake --build build --target IDA`
Expected: links green (ripple guarded).

- [ ] **Step 9: Commit**

```bash
git add core/include/ida/TapePool.h core/src/TapePool.cpp tests/TapePoolTests.cpp persistence app engine
git commit -m "feat: TapePool allows an empty pool and optional primary (blank-slate foundation)"
```

---

## Slice 2 — IDA Project unit + project-scoped tape storage

**Spec refs:** §2.2 (project owns tapes; folder `yyyymmddhhmmss-<name>` grouper; files `tape_<x>`; stable folder id vs display name), §2.1 (no orphans).
**Depends on:** Slice 1.
**Files:** new `IdaProject` type (core/persistence) — `{ folderId: string (yyyymmddhhmmss-<sanitized-name>), displayName, createdTimestamp }`; a path helper `projectTapesDir(project)` building on `idaAppSupportDirectory()`; a tape filename builder `tape_<x>`; migrate the tape store root (currently `…/IDA/tapes/`, commit 942ba5b) to `…/IDA/<folderId>/`.
**Key interfaces:** `std::string IdaProject::folderName()` (sanitized, immutable); `juce::File tapeFile(const IdaProject&, TapeId)` → `<root>/<folderId>/tape_<x>`; display-name setter that does **not** touch the folder.
**Test strategy (headless):** sanitization (spaces/illegal chars → safe); folder name format; `tape_x` mapping from `TapeId`; rename changes display name only; two same-named projects get distinct folders (timestamp disambiguates). Use an injected timestamp (don't call the clock in pure tests).
**Done when:** given a project + `TapeId`, the code resolves a stable, collision-free path under the project folder; no tape path exists outside a project folder.

## Slice 3 — Blank-slate boot + New Song (with deliberate-erase warning)

**Spec refs:** §4.1, §2.1 (erase warning), §11 (boot path swap), §2.2 (New Song = new project, never deletes).
**Depends on:** Slices 1–2.
**Files:** new blank-session builder (replaces `demo_(buildDemoSession())` at `app/MainComponent.cpp:4176` and the `UndoStack` seed at 4177); a **New Song** command (menu + button); the deliberate-erase confirmation dialog; retire `buildDemoSession()` from boot (`app/DemoSession.{h,cpp}` — verify `DemoSessionTests.cpp`/other fixtures construct their own trees before removing, per §11).
**Key behavior:** New Song with no tapes → straight to blank; with tapes → warning ("DELETE an IDA project… cannot be recovered… phrases recreatable, tapes not") → confirm deletes that project's tapes + opens a fresh `Untitled` project (default name, no naming prompt — spec §15.3); cancel = no change.
**Test strategy:** headless for the blank-session builder (empty root Constituent, empty `TapePool`, zero channels) and the erase-guard decision (tapes-present ⇒ requires-confirm); GUI-verified for the dialog + boot.
**Done when:** the app boots blank (no demo), New Song returns to blank, and erasing tapes is impossible without the confirm.

## Slice 4 — Explicit channel creation + 1:1 auto-tape + recording-iff-assigned

**Spec refs:** §5 (Add Channel, pick input, capped, 1:1), §2 (assignment-gated recording), §3 (two layers), §6 (advanced N:1 not precluded).
**Depends on:** Slices 1–3.
**Files:** `rebuildInputStrips()` (`app/MainComponent.cpp:7921`) → build only user-created channels (not one-per-physical-pair); an **Add Channel** affordance (choose an unused physical input pair, capped at the device's input count); auto-mint a `tape_x` for the new channel and assign it; the capture wiring (`TapeRecordWriter` construction ~4255–4320) so a tape writes **iff ≥1 input assigned**; remove the default input strip's tape-destination picker (`refreshInputDestinations` ~7734) — tape is implicit; unpin `TapeId{1}` in `InputMixer` (finishing Slice 1's guard); `CaptureSession` terminology split (rename `Armed`/`AwaitingOut` to marking sub-states; tape recording is assignment-gated, spec §3 note).
**Key behavior:** create channel ⇒ its tape records immediately; remove channel ⇒ tape stops but is retained (archive, not orphan, not auto-deleted).
**Test strategy (headless):** assignment ⇒ recording-on, last-unassign ⇒ recording-off, retained tape still in pool; arm flip happens off the audio thread (RT-safety). GUI-verified: Add Channel, input picker, cap behavior.
**Done when:** a user creates a channel, picks an input, and that source is recording — with no tape vocabulary in the flow.

## Slice 5 — Per-phrase state machine + source-agnostic command layer + transport

**Spec refs:** §8 (per-phrase state, unlimited phrases/loops, coinit, loop-0-tracks-phrase), §8.1 (transitions), §8.2 (stop semantics), §8.3 (maps onto `promote()`), §8.4 (source-agnostic commands), §9 (OttoHost transport).
**Depends on:** Slice 4.
**Files:** a new **CaptureCommand** layer (e.g. `Record`, `Stop` dispatched into the state machine) that GUI / MIDI / pedal all feed; formalize the per-phrase (phrase,loop) state over `core/include/ida/Promotion.h` + `CaptureSession`; wire Record-while-stopped → `OttoHost` play via the existing `TransportBarHost::playPauseClicked()` path (spec §15.1 — capability exists); consolidate bottom-bar Arm/Mark-In/Mark-Out into a Record toggle (optional UI detail).
**Test strategy (headless):** every combined-state transition; coinit (state 6) births phrase + loop 0 with identical bounds; stop sets phrase end = stop, loop end = parent phrase end; playhead-inside ⇒ new loop, playhead-outside ⇒ new phrase; the command layer is input-source-agnostic (same command from a fake "finger" and a fake "MIDI" source yields identical state). GUI-verified: record→phrase appears→plays.
**Done when:** pressing Record from stopped creates a phrase + first loop that plays back; the state machine is fully unit-tested and driven through the command layer.

## Slice 6 — Play all loops of a phrase

**Spec refs:** §8.5 (play = all loops), spec note lifting the T0b "first leaf-loop only" limit.
**Depends on:** Slice 5.
**Files:** `refreshOutputMixerPhraseChannels()` (`app/MainComponent.cpp:7105`) + the `TapePrefetcher` / `OutputMixer` / `Bus` wiring — create **one OutputMixer channel per loop** (keyed by the leaf-loop ConstituentId, matching `PlaybackResolver`), labeled `T#P#L#`, and **sum a phrase's loop-channels at a per-phrase bus** → master (spec §8.6). Supersedes the earlier single-phrase-channel approach.
**Test strategy:** headless where the loop-enumeration is pure; otherwise operator-verified that a phrase with two layered loops plays both. Mind RT-safety on the mixing path.
**Done when:** every loop of a phrase is its own `T#P#L#` Output Mixer channel; a phrase with ≥2 loops plays all of them, balanced via their per-loop faders and summed at the per-phrase bus.

## Slice 7 — Tapes tab = per-input archive + reveal-in-storage

**Spec refs:** §7.
**Depends on:** Slices 2, 4.
**Files:** the Tapes tab (`tapesPane_`, `app/MainComponent.cpp` ~5940) — list each tape by `tape_x` index + the input feeding it; a **recording indicator** (lit ⟺ ≥1 assigned input); a **Reveal-in-storage** button per line → `juce::File::revealToUser()` (guard `NSWorkspace`/`UIApplication` for AUv3 extension-safety; iOS Files-app limits).
**Test strategy:** GUI-verified (reveal opens the right folder); headless for the recording-indicator predicate.
**Done when:** the user finds each input's complete take and reveals its file on disk.

## Slice 8 — Phrase-trigger button bank

**Spec refs:** §8.5 (layout `< 1..8 >` under the top transport bar; banks of 8; positional mapping `(b-1)*8+p`; empty slots inert; press drives the phrase's multi-mode state; traditional colors via `IdaPalette`; context menu Clear/Copy/Paste/Rename/Assign MIDI…; default numeric labels, renameable; right-click = long-press).
**Depends on:** Slice 5 (state machine + command layer); Slice 6 (so "play" plays all loops).
**Files:** a new phrase-button-bank component placed as a horizontal row directly beneath the top-bar transport area; bank paging via chevrons; per-button state→color through `ui/include/ida/IdaPalette.h` (rec=red, play=green, overdub=amber, empty/stopped=dim) reconciled with the phrase identity colour method (`docs/design/ida-colour-method.md`); the context menu (Clear removes the phrase only, never its tape — §2.1; all ops undoable); per-button MIDI note/channel/port storage (assignment UI now; live MIDI trigger is the future §14 work).
**Test strategy (headless):** positional bank math (`(b-1)*8+p`), state→color mapping, empty-slot inertness; the press dispatches the same command as Slice 5's layer. GUI-verified: layout, paging, colors, context menu, rename.
**Done when:** existing phrases appear as colored, labeled, paged buttons that trigger their state and expose the context menu; empty slots are inert.

---

## Self-review

- **Spec coverage:** §2/§2.1/§2.2 → Slices 1–3; §3 → Slices 4–5; §4 → Slices 3–6 (the walk); §5 → Slice 4; §6 → noted not-precluded (Slice 4); §7 → Slice 7; §8/§8.1–8.5 → Slices 5, 6, 8; §9 → Slice 5; §10 (undo) → cross-cutting; §11 → Slices 1, 3, 4; §12 → the separate doc-update plan; §13 (tests) → per-slice strategy; §15 decisions → folded (OTTO transport Slice 5, channel-undo cross-cutting, default name Slice 3). No spec section is unassigned. ✓
- **Decomposition:** eight slices, each independently testable; Slice 1 detailed, 2–8 to be detail-planned at execution. This is the skill's "break a multi-subsystem spec into separate plans." ✓
- **Type consistency:** `primary()` is `std::optional<TapeId>` everywhere after Slice 1; `tape_<x>` filename + `yyyymmddhhmmss-<name>` folder are the single naming convention (Slice 2) referenced by Slices 4 and 7. ✓
