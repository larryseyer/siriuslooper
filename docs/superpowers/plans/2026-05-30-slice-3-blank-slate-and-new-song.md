# Slice 3 — Blank-Slate Boot + New Song + Deliberate-Erase Warning — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Retire the demo song as IDA's boot state. The app boots into a blank, usable project (`Untitled`): empty root Constituent, empty `TapePool`, zero channels. A **New Song** command (menu + button) returns to that blank state. New Song **never** deletes tapes — it opens a fresh project. The only path that destroys tapes is a separate, deliberate, unambiguously warned **erase**: with tapes present it shows a warning the user must confirm; on confirm it deletes that project's tape folder and opens a fresh `Untitled`; on cancel nothing changes. With no tapes, New Song goes straight to blank with no prompt.

**Architecture:** A pure, JUCE-free **blank-session builder** in `core` (`ida::buildBlankSession()`) returns a struct shaped exactly like the fields `MainComponent` consumes from `DemoSession` (`root`, `sessionToLmc`, `lengthLmcSeconds`, `inputs`), so the constructor swap at `app/MainComponent.cpp:4176-4182` is a one-line type/initializer change. A pure **erase-guard predicate** (`ida::eraseRequiresConfirmation(const TapePool&)`) decides "tapes present ⇒ must confirm" and is unit-tested headless. `buildDemoSession()` stays in the codebase for `tests/DemoSessionTests.cpp` (which IS the demo-shape test and constructs its tree solely through it) but is removed from the boot path and from the `reloadDemo()` affordance, which is repurposed into the New Song / erase flow. The New Song command, the deliberate-erase `AlertWindow`, and the project tape-folder deletion are GUI/app wiring (operator-verified), modeled on the existing async-`AlertWindow` pattern (`app/FileInputPlayerWindow.mm:683`) and the load-path full-reset bracket (`app/MainComponent.cpp:8833-8890`).

**Tech Stack:** C++/JUCE; `core` (JUCE-free pure C++) for the blank builder + erase predicate; `app` for the GUI command/dialog; Catch2 (`IdaTests`, flat `tests/`, listed in `tests/CMakeLists.txt`); CMake + Ninja. Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` (§4.1, §2.1, §2.2, §11, §15.3).

**Dependencies:**
- **Slice 1 (assume done):** `ida::TapePool` may be empty; default ctor seeds **no** tapes; `primary()` returns `std::optional<TapeId>`; `remove()` can empty the pool; the explicit-list ctor + `SessionFormat` accept an empty pool. Slice 1 also guards the `primary()` compile ripple in `InputMixer` and the `MainComponent` ctor seeding. **`ida::mirrorTapePool(pool, mixer)` MUST be a no-op on an empty pool** — verify (it is exercised by the blank boot). See *Cross-slice risk R1* for `primary()` call sites Slice 1's step list does **not** name.
- **Slice 2 (assume done):** an `ida::IdaProject` type — `{ folderId (string `yyyymmddhhmmss-<sanitized-name>`), displayName, createdTimestamp }` with `folderName()` (sanitized, immutable) and a display-name setter — plus a path helper that resolves a project's tape directory from `idaAppSupportDirectory()`. This plan calls that helper **`projectTapesDir(const IdaProject&)`** and assumes Slice 2 added a `MainComponent` member **`currentProject_`** holding the live project. See *Cross-slice risk R2* — if Slice 2 did not add `currentProject_`, Task 4 Step 1 adds it.

---

## Cross-slice risks the roadmap under-specified (read before starting)

- **R1 — `primary()` optional ripple beyond Slice 1's named sites.** Slice 1 Step 7 names only `InputMixer` and "the `MainComponent` ctor pool-seeding / `refreshInputDestinations`". But `primary()` is also consumed at, at least: `app/MainComponent.cpp:8265` (`focusedTape_ = tapePool_.primary();` — assigns into a `TapeId`), `app/MainComponent.cpp:8286` (`tapesPane_->setTapes (infos, tapePool_.primary());`), and the load-path snapshot `tape.id != tapePool_.primary()` (~`8861`). If Slice 1 left these unguarded the `IDA` target will not compile. **Before Task 1, run `grep -rn "\.primary()\|->primary()" app engine ui | grep -v test` and confirm every site compiles against `std::optional<TapeId>`.** If any are unguarded, fix them as the first commit of Task 4 (they are squarely in the blank-slate ripple this slice owns). Do not paper over with `.value()` where the empty case is reachable — guard it.
- **R2 — `currentProject_` ownership.** This slice both *reads* `currentProject_` (to find the tape folder to erase) and *replaces* it (New Song mints a new `IdaProject`). The cleanest seam is for Slice 2 to own the member and the boot-time construction; this plan assumes that and only swaps it. If Slice 2 did not add it, Task 4 adds `ida::IdaProject currentProject_;` initialized to a fresh `Untitled` project at construction.
- **R3 — `mirrorTapePool` / `tapeColoringSink_` on an empty pool.** The blank boot path runs `mirrorTapePool(tapePool_, *inputMixer_)` and the `tapeColoringSink_->addTape` seed loop over **zero** tapes. The seed loop is naturally a no-op; `mirrorTapePool` must tolerate empty (R1/Slice 1). Verified by the blank boot bringing the app up (Task 5).

---

## Task 1 — Pure blank-session builder (`core`, headless TDD)

A JUCE-free builder returning the same fields `MainComponent` consumes from `DemoSession`. It produces an empty root Constituent (one childless session shell), a valid `TempoMap`, a zero session length, and **no** inputs — so the ctor swap is mechanical and every downstream `demo_.X` read keeps working.

**Files:**
- Create: `core/include/ida/BlankSession.h`
- Create: `core/src/BlankSession.cpp`
- Create test: `tests/BlankSessionTests.cpp`
- Modify: `core/CMakeLists.txt` (add `src/BlankSession.cpp` to the `core` target — match how `src/TapePool.cpp` is listed)
- Modify: `tests/CMakeLists.txt` (add `BlankSessionTests.cpp` to the `IdaTests` sources, alongside `DemoSessionTests.cpp`)

- [ ] **Step 1: Write the failing test `tests/BlankSessionTests.cpp`**

```cpp
// BlankSession is the boot state that replaces the demo song (spec §4.1, §11).
// It must be a *usable empty project*: an empty root Constituent (no phrases),
// no tapes, no channels. These tests lock that emptiness so a future edit that
// reseeds the boot with demo-like content surfaces immediately.

#include "ida/BlankSession.h"

#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/Rational.h"

#include <catch2/catch_test_macros.hpp>

using namespace ida;

TEST_CASE ("buildBlankSession has an empty root Constituent (no phrases)", "[blankSession]")
{
    const auto blank = buildBlankSession();
    REQUIRE (blank.root != nullptr);
    CHECK (blank.root->children().empty());     // zero phrases at boot
    CHECK (blank.root->isLeaf());
    CHECK_FALSE (blank.root->isPhrase());        // the shell is a bare container
    CHECK_FALSE (blank.root->tapeReference().has_value());
}

TEST_CASE ("buildBlankSession has no inputs and no tapes referenced", "[blankSession]")
{
    const auto blank = buildBlankSession();
    CHECK (blank.inputs.empty());                // zero channels at boot (spec §4.1)
}

TEST_CASE ("buildBlankSession reports a zero session length", "[blankSession]")
{
    const auto blank = buildBlankSession();
    CHECK (blank.lengthLmcSeconds == Rational (0));
    CHECK (blank.root->conceptualOut() == Position (Rational (0)));
}
```

- [ ] **Step 2: Run the test, verify it FAILS to compile/link**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `ida/BlankSession.h` does not exist.

- [ ] **Step 3: Create `core/include/ida/BlankSession.h`**

```cpp
#pragma once

#include "ida/Constituent.h"
#include "ida/InputDescriptor.h"
#include "ida/Rational.h"
#include "ida/TempoMap.h"

#include <memory>
#include <vector>

namespace ida
{

/// The boot / New-Song state (spec §4.1, §11): a *usable empty project*. Shaped
/// identically to DemoSession in the fields MainComponent consumes, so swapping
/// the constructor's `demo_(buildDemoSession())` for `buildBlankSession()` is a
/// mechanical change and every `demo_.<field>` read keeps working unchanged.
/// Unlike DemoSession there is no pre-authored arrangement: the root Constituent
/// is an empty session shell, there are no inputs, and the span is zero.
struct BlankSession
{
    std::shared_ptr<const Constituent> root;   ///< empty session shell (no children)
    TempoMap sessionToLmc;                       ///< a valid default tempo map
    Rational lengthLmcSeconds;                   ///< zero — nothing is placed yet
    std::vector<InputDescriptor> inputs;         ///< empty — channels are user-created (spec §4.1)
};

/// Builds the blank boot session. Pure (JUCE-free), deterministic, no clock /
/// filesystem access. The session shell carries the canonical root id (1) and a
/// [0,0) span; the default tempo map (120 BPM) places it in LMC time so the
/// existing RenderPipeline / timeline math has a real map to apply even though
/// nothing is placed yet.
BlankSession buildBlankSession();

} // namespace ida
```

- [ ] **Step 4: Create `core/src/BlankSession.cpp`**

```cpp
#include "ida/BlankSession.h"

#include "ida/ConstituentId.h"
#include "ida/Position.h"

namespace ida
{

BlankSession buildBlankSession()
{
    // Root id 1 matches the demo session's root id so any code keyed to "the
    // session shell is id 1" keeps holding. A [0,0) span is a valid leaf
    // Constituent (conceptualOut does not precede conceptualIn) and reads as
    // "nothing placed yet". The shell is deliberately NOT a phrase and carries
    // no tape reference — it is a bare container the user fills via the Record
    // gesture (spec §8, a later slice).
    const Constituent shell =
        Constituent (ConstituentId (1), Position(), Position (Rational (0)))
            .withName ("Untitled");
    auto root = std::make_shared<const Constituent> (shell);

    // A real default tempo map so timeline/render math has something to apply;
    // length is zero because nothing is placed. fromBpm is pure.
    TempoMap sessionToLmc = TempoMap::fromBpm (Rational (120));

    return BlankSession { std::move (root), std::move (sessionToLmc),
                          Rational (0), {} };
}

} // namespace ida
```

- [ ] **Step 5: Register the new source + test in CMake**

In `core/CMakeLists.txt`, add `src/BlankSession.cpp` to the `core` target's source list (find the line listing `src/TapePool.cpp` and add the new file beside it, preserving the existing formatting). In `tests/CMakeLists.txt`, add `BlankSessionTests.cpp` to the `IdaTests` source list immediately after `DemoSessionTests.cpp`.

- [ ] **Step 6: Run the tests, verify PASS**

Run: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests && ctest --test-dir build -R blankSession`
Expected: PASS — all three `[blankSession]` cases green.

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/BlankSession.h core/src/BlankSession.cpp core/CMakeLists.txt tests/BlankSessionTests.cpp tests/CMakeLists.txt
git commit -m "feat: add pure blank-session builder for the first-run boot state"
```

Trailer (append in the commit body):
```
Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
```

---

## Task 2 — Pure erase-guard predicate (`core`, headless TDD)

The single decision "does erasing this project's recordings require a deliberate confirmation?" — `true` iff the project has ≥1 tape. Pure and tiny, but it encodes the PARAMOUNT §2.1 safety rule, so it is unit-tested in its own right and the GUI calls it rather than re-deriving the condition inline.

**Files:**
- Modify: `core/include/ida/BlankSession.h` (declare `eraseRequiresConfirmation` — co-located: both are boot/New-Song pure helpers)
- Modify: `core/src/BlankSession.cpp` (define it)
- Modify: `tests/BlankSessionTests.cpp` (add the predicate cases)

- [ ] **Step 1: Add failing tests to `tests/BlankSessionTests.cpp`**

Add this `#include` near the top (after the existing includes):

```cpp
#include "ida/TapePool.h"
```

Then append:

```cpp
TEST_CASE ("eraseRequiresConfirmation is false for an empty project", "[blankSession][erase]")
{
    ida::TapePool empty;                          // Slice 1: default ctor is empty
    CHECK_FALSE (ida::eraseRequiresConfirmation (empty));
}

TEST_CASE ("eraseRequiresConfirmation is true once any tape exists", "[blankSession][erase]")
{
    ida::TapePool pool;
    pool.add ("Tape 1");
    CHECK (ida::eraseRequiresConfirmation (pool));   // §2.1: tapes present ⇒ must warn
    pool.add ("Tape 2");
    CHECK (ida::eraseRequiresConfirmation (pool));
}
```

- [ ] **Step 2: Run, verify FAIL**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — `eraseRequiresConfirmation` is undeclared.

- [ ] **Step 3: Declare it in `core/include/ida/BlankSession.h`**

Add `#include "ida/TapePool.h"` to the header's includes, and add this declaration inside `namespace ida` (below `buildBlankSession`):

```cpp
/// PARAMOUNT safety predicate (spec §2.1). Returns true iff erasing this
/// project's recordings would destroy at least one irreplaceable tape — i.e.
/// the pool is non-empty. The New-Song / erase flow uses this to decide whether
/// to show the deliberate-erase warning: empty ⇒ no prompt (nothing to lose);
/// non-empty ⇒ the user MUST confirm before any tape is deleted. Pure.
bool eraseRequiresConfirmation (const TapePool& pool) noexcept;
```

- [ ] **Step 4: Define it in `core/src/BlankSession.cpp`**

Add at the bottom of `namespace ida`:

```cpp
bool eraseRequiresConfirmation (const TapePool& pool) noexcept
{
    return pool.count() > 0;
}
```

- [ ] **Step 5: Run, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "blankSession"`
Expected: PASS — all `[blankSession]` cases (builder + erase) green.

- [ ] **Step 6: Commit**

```bash
git add core/include/ida/BlankSession.h core/src/BlankSession.cpp tests/BlankSessionTests.cpp
git commit -m "feat: add eraseRequiresConfirmation predicate guarding tape deletion"
```

Append the `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` trailer.

---

## Task 3 — Swap the boot path: blank session replaces the demo (app)

Replace `demo_(buildDemoSession())` and its derived initializers and the post-construction "renamed session" demo edit. `buildDemoSession()` is **kept** in the tree for `DemoSessionTests.cpp` but no longer runs at boot. This task is the constructor surgery only; the resulting app boots blank but New Song does not exist yet (Task 4).

**Files:**
- Modify: `app/MainComponent.h` — change the `demo_` member type (`app/MainComponent.h:340`); add the `BlankSession.h` include (`app/MainComponent.h:4`).
- Modify: `app/MainComponent.cpp` — ctor initializer list (`:4176-4182`); the seed-`nextConstituentId_` walk reads `*undoStack_.current()` already and is fine; remove the "renamed session" demo edit (`:4207-4212`); the tape-pool seed block becomes a no-op for an empty `inputs_` (already guarded at `:4297` — verify); `reloadDemo()` body (`:9078-9088`) is handled in Task 4.

- [ ] **Step 1: Add the blank-session include to `app/MainComponent.h`**

At `app/MainComponent.h:4`, the line is `#include "DemoSession.h"`. Add directly below it:

```cpp
#include "ida/BlankSession.h"
```

(Keep the `DemoSession.h` include — `buildDemoSession()` and the `DemoSession` type stay referenced by the kept demo path / tests, and `DemoSession.h` is what currently declares the consumed field shape; the member type change in Step 2 is what swaps the boot.)

- [ ] **Step 2: Change the `demo_` member type to `BlankSession`**

`app/MainComponent.h:340` currently:

```cpp
    DemoSession  demo_;
```

Change to:

```cpp
    // Boot state. Despite the legacy member name, this is the BLANK session
    // (spec §4.1/§11) — an empty project, not the retired demo song. The name is
    // kept to minimize churn across the ~15 `demo_.<field>` read sites; all the
    // fields BlankSession exposes match the ones those sites read.
    BlankSession  demo_;
```

- [ ] **Step 3: Swap the constructor initializer**

`app/MainComponent.cpp:4176-4182` currently:

```cpp
    : demo_ (buildDemoSession()),
      undoStack_ (demo_.root),
      sessionLengthSeconds_ (demo_.lengthLmcSeconds),
      tier_ (demoTier()),
      tierPolicy_ (policyFor (tier_)),
      inputs_ (demo_.inputs),
      focusedTape_ (! demo_.inputs.empty() ? demo_.inputs.front().tapeId
                                           : TapeId (0))
```

Change only the first line:

```cpp
    : demo_ (buildBlankSession()),
```

Leave the rest of the initializer list verbatim — `demo_.root`, `demo_.lengthLmcSeconds`, `demo_.inputs` are all valid `BlankSession` fields. `focusedTape_` resolves to `TapeId(0)` because `demo_.inputs` is empty (the blank ternary's else-branch), which is the correct "no focused tape" sentinel.

- [ ] **Step 4: Remove the demo "renamed session" undo edit**

`app/MainComponent.cpp:4207-4212` currently:

```cpp
    // Demo edit so undo/redo have something to traverse: a renamed session.
    {
        const auto renamed = std::make_shared<const Constituent> (
            demo_.root->withName ("renamed session"));
        undoStack_.push (renamed, "rename session");
    }
```

Delete this entire block — it exists only to give the demo an undo step to traverse. A blank project boots with a clean, empty undo history (the first real undoable step is the user's first channel/phrase creation in a later slice). Do not replace it with anything.

- [ ] **Step 5: Confirm the tape-pool seed block is already empty-safe**

`app/MainComponent.cpp:4297` guards `if (! demo_.inputs.empty())` before seeding the pool, so with the blank session's empty `inputs_` the pool stays the Slice-1 empty default and the `tapeColoringSink_->addTape` loop at `:4313` iterates zero tapes. **Read these lines and confirm no `demo_.inputs.front()` / `primary()` is dereferenced outside that guard.** No code change expected; if one is found, guard it (it is blank-slate ripple this slice owns).

- [ ] **Step 6: Build the app, verify it links**

Run: `cmake --build build --target IDA`
Expected: links green. (If it fails on a `primary()` optional site, that is R1 — fix the named sites here as part of this commit.)

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: boot IDA into a blank session instead of the demo song"
```

Append the `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` trailer.

---

## Task 4 — New Song command + deliberate-erase flow (app; engine predicate already TDD'd)

Repurpose the existing "Reload demo" button into a **New Song** button, add a matching right-click menu item (paired with long-press for iOS per the cross-cutting rule), and implement `newSong()`: if `eraseRequiresConfirmation(tapePool_)` is false, go straight to blank; otherwise show the §2.1 warning `AlertWindow`. On cancel, nothing changes. On confirm, delete the current project's tape folder, then open a fresh `Untitled` project and reset to blank. The reset mirrors the load-path bracket so the audio callback is detached while the pool/mixer/sink are replaced.

**Files:**
- Modify: `app/MainComponent.h` — add `void newSong();` and `void resetToBlankProject();` declarations near `reloadDemo()` (`app/MainComponent.h:337`); if Slice 2 did not add it, add `ida::IdaProject currentProject_;` (see R2). Add `#include "ida/IdaProject.h"` if not already pulled in by Slice 2.
- Modify: `app/MainComponent.cpp` — rename the "Reload demo" button text + wiring (`:291`, `:4523`); replace `reloadDemo()` (`:9078`) with `newSong()` + `resetToBlankProject()`; add the right-click/long-press menu entry.
- Modify (button label only): the `PreparationPane` button getter name may stay `reloadDemoButton()` to minimize churn, OR be renamed; this plan keeps the getter name and only changes the user-visible text + the click handler (decision noted inline).

- [ ] **Step 1: Add declarations + (if needed) the project member to `app/MainComponent.h`**

Near `app/MainComponent.h:337` (`void reloadDemo();`), replace that line with:

```cpp
    // --- New Song / blank project (spec §4.1, §2.1, §2.2) ---
    void newSong();              ///< menu/button entry; warns before erasing tapes
    void resetToBlankProject();  ///< swap to a fresh Untitled blank project (no warning)
```

If Slice 2 did **not** already add a current-project member (check first: `grep -n "currentProject_" app/MainComponent.h`), add near the session-state members (`app/MainComponent.h:339`, beside `DemoSession demo_;`):

```cpp
    // The live IDA project that owns the current tapes (spec §2.2). New Song
    // replaces it with a fresh Untitled project; the deliberate erase deletes
    // this project's tape folder before the swap. (Added by Slice 2 if present.)
    ida::IdaProject currentProject_;
```

and ensure `#include "ida/IdaProject.h"` is present in the header includes.

- [ ] **Step 2: Rename the button text + click handler (no functional erase yet — temporary, to verify wiring)**

At `app/MainComponent.cpp:291`:

```cpp
        reloadDemoButton_.setButtonText ("Reload demo");
```

change to:

```cpp
        reloadDemoButton_.setButtonText ("New Song");
```

At `app/MainComponent.cpp:4523`:

```cpp
    preparationPane_->reloadDemoButton().onClick = [this] { reloadDemo(); };
```

change to:

```cpp
    preparationPane_->reloadDemoButton().onClick = [this] { newSong(); };
```

(The getter/member keep the legacy `reloadDemo` name to avoid a wide rename; the user never sees it. A one-line comment at the member declaration `app/MainComponent.cpp:399` should note "labeled 'New Song'; legacy member name".)

- [ ] **Step 3: Replace `reloadDemo()` with `newSong()` + `resetToBlankProject()`**

Replace the entire `reloadDemo()` body (`app/MainComponent.cpp:9078-9088`) with the two functions below. `resetToBlankProject()` factors the swap so both the no-tape fast path and the post-confirm path share it.

```cpp
void MainComponent::resetToBlankProject()
{
    // Mint a fresh empty project (default name Untitled, no naming prompt —
    // spec §15.3). Slice 2 owns IdaProject construction; this asks for a new
    // Untitled with a fresh creation timestamp so its folder is unique.
    currentProject_ = ida::IdaProject ("Untitled");

    const auto blank = buildBlankSession();

    // Swap engine state with the audio callback detached, mirroring the
    // load-path bracket (MainComponent.cpp ~8833): stop callback → release the
    // old pool's tapes from the InputMixer + TAPECOLOR decorator → install the
    // empty pool → restart callback. With a blank session the new pool is
    // empty, so there is nothing to re-add — only the old terminals are pruned.
    audioDeviceManager_.removeAudioCallback (audioCallback_.get());

    for (const auto& tape : tapePool_.tapes())
    {
        tapeRecordWriter_->closeTape (tape.id);     // SPSC: inside the bracket only
        inputMixer_->removeTape (tape.id);
        if (tapeColoringSink_ != nullptr)
            tapeColoringSink_->removeTape (tape.id);
    }

    tapePool_ = ida::TapePool {};                    // Slice 1: empty pool is legal
    ida::mirrorTapePool (tapePool_, *inputMixer_);    // R3: no-op on empty pool

    audioDeviceManager_.addAudioCallback (audioCallback_.get());

    // Reset structure/session state to the blank tree. Pushed as a fresh,
    // labeled undo step so the operator can undo back to whatever was on
    // screen (white paper Part 14.7: a swap is an edit, not a history wipe).
    inputs_ = blank.inputs;                           // empty
    sessionLengthSeconds_ = blank.lengthLmcSeconds;   // zero
    demo_ = blank;                                    // keep the consumed-fields source in step
    nextConstituentId_ = blank.root->id().value() + 1;
    focusedTape_ = TapeId (0);                         // no tape to focus
    armedTapeIds_.clear();
    if (captureSession_.isArmed())
        captureSession_.disarm();
    phraseChannelByConstituent_.clear();
    undoStack_.push (blank.root, "New Song");

    rebuildInputStrips();                  // zero channels now
    refreshOutputMixerPhraseChannels();    // zero phrase strips now
    refreshTapesPane();
    refreshPerformance();
    refreshPreparation();
    preparationPane_->setStatus ("New song (Untitled)");
}

void MainComponent::newSong()
{
    // PARAMOUNT §2.1: if the current project holds no tapes, there is nothing
    // irreplaceable to lose — go straight to a blank project, no prompt.
    if (! ida::eraseRequiresConfirmation (tapePool_))
    {
        resetToBlankProject();
        return;
    }

    // Tapes present ⇒ New Song would abandon this project's recordings. New Song
    // itself NEVER deletes (spec §2.2): the only destructive path is the
    // deliberate erase below, behind an unambiguous warning. The button does the
    // safe thing — it offers to start a new project AND, distinctly, to delete
    // the current one's recordings. We present the destructive choice explicitly.
    auto* aw = new juce::AlertWindow (
        "Delete this IDA project's recordings?",
        "You are about to DELETE this IDA project (\""
            + juce::String (currentProject_.displayName())
            + "\") and start a new one.\n\n"
              "Its recordings (tapes) CANNOT be recovered. Phrases can be "
              "recreated later, but the underlying tapes cannot.\n\n"
              "Choose \"Delete recordings\" to erase this project and open a new "
              "blank song, or \"Cancel\" to keep everything as it is.",
        juce::MessageBoxIconType::WarningIcon,
        this);

    // Destructive button is NOT the default/return key — the operator must
    // deliberately choose it. Escape cancels.
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->addButton ("Delete recordings", 1);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create (
            [this, aw] (int result)
            {
                std::unique_ptr<juce::AlertWindow> owned (aw);
                if (result != 1)
                    return;   // Cancel — nothing changes (spec §2.1)

                // Confirmed: delete THIS project's tape folder, then swap to a
                // fresh blank Untitled project. Capture the folder BEFORE the
                // swap replaces currentProject_.
                const juce::File doomedTapes = projectTapesDir (currentProject_);

                // Tapes must stop writing before the folder is deleted: detach
                // the callback and close every open tape, then delete on disk.
                audioDeviceManager_.removeAudioCallback (audioCallback_.get());
                for (const auto& tape : tapePool_.tapes())
                {
                    tapeRecordWriter_->closeTape (tape.id);
                    inputMixer_->removeTape (tape.id);
                    if (tapeColoringSink_ != nullptr)
                        tapeColoringSink_->removeTape (tape.id);
                }
                audioDeviceManager_.addAudioCallback (audioCallback_.get());

                if (doomedTapes.isDirectory())
                {
                    const bool deleted = doomedTapes.deleteRecursively();
                    if (! deleted)
                    {
                        // Fail loud: do NOT proceed to a new project while the
                        // old recordings still occupy disk under a name the
                        // user just asked to erase. Surface it and stop.
                        preparationPane_->setStatus (
                            "Could not delete recordings at "
                            + doomedTapes.getFullPathName()
                            + " — project unchanged.");
                        // Re-mirror the (still-present) pool so the engine and
                        // UI stay consistent after the close/remove above.
                        ida::mirrorTapePool (tapePool_, *inputMixer_);
                        refreshTapesPane();
                        return;
                    }
                }

                resetToBlankProject();
            }));
}
```

> Note on `IdaProject` construction/accessors: this plan uses `ida::IdaProject("Untitled")` (constructs a fresh project with a creation timestamp + that display name), `currentProject_.displayName()`, and `projectTapesDir(currentProject_)`. If Slice 2 named these differently (e.g. a factory `IdaProject::createNew("Untitled")` or `folderName()` for the path), adapt these three call sites to Slice 2's actual API — they are the only coupling points. Do not invent a second project type.

- [ ] **Step 4: Add the right-click / long-press menu entry for New Song**

The Preparation pane's New Song affordance is a button (Step 2). To satisfy the cross-cutting "every right-click pairs with long-press" rule for a *menu* path, add a "New Song…" item to the existing timeline/preparation context menu rather than inventing a new gesture. Locate the Preparation context-menu construction (the `onPillContextMenuRequested` lambda at `app/MainComponent.cpp:4527` builds a `juce::PopupMenu`); add a top-level New Song item to the menu that the preparation surface already opens on right-click. Concretely, in that lambda's `juce::PopupMenu menu;` add **before** the existing items:

```cpp
        menu.addItem ("New Song", [this] { newSong(); });
        menu.addSeparator();
```

This menu is opened via `showMenuAsync(...withMousePosition())`, and JUCE's `PopupMenu` opens on long-press on iOS automatically when triggered from a touch — so the same item is reachable by right-click (desktop) and long-press (iOS) through one code path, honoring the rule. (If a dedicated New-Song-only context gesture is wanted later, it reuses this same `newSong()` command — do not duplicate the logic.)

- [ ] **Step 5: Build the app, verify it links**

Run: `cmake --build build --target IDA`
Expected: links green. If `projectTapesDir` / `IdaProject` symbols are missing, that is the Slice 2 coupling (R2) — reconcile the call sites with Slice 2's actual names, do not stub.

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: New Song command with deliberate-erase warning before tape deletion"
```

Append the `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` trailer.

---

## Task 5 — Clean rebuild + operator GUI verification

The blank boot, the New Song button, and the erase dialog are GUI wiring — operator-verified, never unit-faked (repo convention). A clean build is mandatory before hand-off (CMake caches stale configs; `[[feedback_clean_builds_only_for_testing]]`). The agent builds AND launches AND hands the operator terse numbered steps.

**Files:** none (verification only).

- [ ] **Step 1: Clean reconfigure + build the app**

Run:
```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
```
Expected: a fresh `build/app/IDA_artefacts/Release/IDA.app`.

- [ ] **Step 2: Confirm the full test suite still passes (the demo-shape tests must survive)**

Run:
```bash
cmake --build build --target IdaTests
ctest --test-dir build
```
Expected: the baseline pass count holds (449/450; the 1 non-pass is the separately-run `MainComponentPluginEditorTests` exe). Specifically confirm `[demoSession]` cases still PASS (`buildDemoSession()` is intact for tests) and `[blankSession]` cases PASS.

- [ ] **Step 3: Launch the app for the operator**

Run:
```bash
open build/app/IDA_artefacts/Release/IDA.app
```

- [ ] **Step 4: Hand the operator these numbered verification steps**

Provide exactly this checklist (operator performs the GUI gestures the agent cannot):

1. **Boots blank.** On launch, the Preparation tab's timeline/tree shows **no phrases** (no intro/verse/outro), the Tapes tab shows **no tapes**, and the Input Mixer shows **no channels**. (Was: a demo song with 5 phrases + tapes 1–4.)
2. **Button reads "New Song".** The Preparation tab's top row shows a **New Song** button where "Reload demo" used to be.
3. **New Song with no tapes = instant blank, no prompt.** With nothing recorded, click **New Song** → it returns to blank immediately, status reads "New song (Untitled)", **no warning dialog appears**.
4. **New Song with tapes = warning.** (Once a later slice can create a channel/tape — or by loading a saved session that has tapes.) With ≥1 tape present, click **New Song** → a warning dialog appears titled "Delete this IDA project's recordings?" naming the project, stating recordings cannot be recovered.
5. **Cancel changes nothing.** In that dialog, click **Cancel** → the dialog closes, the tapes and phrases are **still present**, status unchanged.
6. **Confirm erases + opens blank.** Re-open New Song with tapes present, click **Delete recordings** → the app returns to blank (no tapes, no phrases), and the prior project's tape folder under `~/Library/Application Support/IDA/<yyyymmddhhmmss-name>/` is **gone** on disk.
7. **Right-click/long-press menu.** Right-click (desktop) or long-press (iOS) on the Preparation timeline surface → the context menu includes a **New Song** item that does the same as the button.

- [ ] **Step 5: On operator confirmation, finalize**

This is a verification gate, not a code change — no commit. If the operator reports a defect, fix it as a follow-on commit (do not amend Task 3/4 commits). When the operator confirms steps 1–7, the slice is done.

---

## Self-review

- **Spec sections covered:**
  - **§4.1 (blank boot — no demo/phrases/song/tapes/channels):** Task 1 (empty builder, headless-locked), Task 3 (boot swap), Task 5 step 4.1 (operator-verified blank boot).
  - **§2.1 (PARAMOUNT — tapes irreplaceable; deletion only behind a deliberate, unambiguous warning; cancel = nothing changes):** Task 2 (`eraseRequiresConfirmation` predicate, headless-tested), Task 4 (warning `AlertWindow` with a non-default destructive button + escape-cancel; cancel returns without touching state; confirm deletes then swaps; fail-loud on delete failure).
  - **§2.2 (New Song = new project, never deletes; project owns tapes; project-scoped folder):** Task 4 (`newSong()` mints a fresh `Untitled` project via Slice 2's `IdaProject`; the no-tape path never deletes; deletion is the explicit erase branch only; uses `projectTapesDir(currentProject_)`).
  - **§11 (boot-path swap; `buildDemoSession()` retired from boot but kept for fixtures):** Task 3 (ctor swap, demo-edit removal), Task 5 step 2 (demo-shape tests still pass — `buildDemoSession()` intact).
  - **§15.3 (default name `Untitled`, no naming prompt):** Task 4 (`ida::IdaProject("Untitled")`, no dialog before reset).
  - Cross-cutting (undo as one labeled step; right-click pairs long-press; clean build before hand-off): Task 4 (`undoStack_.push(blank.root, "New Song")`; one context-menu code path), Task 5 (`rm -rf build`).
- **Placeholder scan:** No `TODO`/`FIXME`/`XXX`/`stub`/`placeholder` introduced. Every code step is real, compilable code. The only conditional work is *adapting to Slice 2's actual `IdaProject` API names* (Task 4 Step 3 note) and *adding `currentProject_` if Slice 2 omitted it* (Task 4 Step 1, R2) — both are concrete reconciliations, not deferrals. `buildDemoSession()` is intentionally retained (not dead code): `tests/DemoSessionTests.cpp` is its sole live caller and asserts the demo shape; deleting it would break the suite (verified: `grep -rn buildDemoSession` shows the boot site at `:4176` and `reloadDemo` at `:9083` are the only non-test callers, both removed/repurposed here).
- **Type/name consistency:** `BlankSession` exposes exactly `{root, sessionToLmc, lengthLmcSeconds, inputs}` — the subset of `DemoSession`'s fields that `MainComponent` reads (`demo_.root`, `demo_.sessionToLmc`, `demo_.lengthLmcSeconds`, `demo_.inputs`), so the member-type swap needs no read-site edits. `primary()` is treated as `std::optional<TapeId>` everywhere (Slice 1); R1 flags the un-named ripple sites (`:8265`, `:8286`, `:8861`) to verify/guard. `eraseRequiresConfirmation(const TapePool&)` is the single name used by both the test and the GUI. `currentProject_` / `ida::IdaProject` / `projectTapesDir()` are the Slice 2 contract names, with the one explicit adapt-if-different note. No new tape can be created or deleted outside the project folder (deletion targets `projectTapesDir(currentProject_)` only) — the no-orphan / §2.1 invariants hold.
