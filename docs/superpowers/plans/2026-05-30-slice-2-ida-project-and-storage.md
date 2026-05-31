# Slice 2 — IDA Project Unit + Project-Scoped Tape Storage + Naming — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Introduce an `IdaProject` value type whose stable, creation-stamped folder id (`yyyymmddhhmmss-<sanitized-name>`) owns its tapes on disk, and route every tape file through one project-scoped path builder so tapes live under `…/IDA/<folderId>/tape_<x>.idatape` with no orphan path anywhere.

**Architecture:** `IdaProject` and its pure naming logic (name sanitization, folder-name format, `tape_<x>` stem) are JUCE-free and live in `core` so they are exhaustively unit-testable with an **injected** timestamp string — no wall-clock call ever runs inside a test. The `juce::File` path builders (`projectTapesDir`, `tapeFileFor`) wrap that pure logic and live in `persistence` (which already depends on `juce_core`). The three current hardcoded `tape-<id>.idatape` sites (writer + two reader sites) are NOT rewired here — Slice 2 only produces the builder; the rewiring is Slice 3/4 work — but the builder is shaped so those sites can converge on it.

**Tech Stack:** C++20/JUCE; `core` (JUCE-free pure C++), `persistence` (`juce_core`); Catch2 (`IdaTests`); CMake + Ninja. Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` §2.2, §2.1.

**Dependencies:** Slice 1 (TapePool: legal empty pool + `std::optional<TapeId> primary()` + `remove()` can empty the pool + `SessionFormat` accepts an empty pool). Slice 2 does not call `primary()`, so it is robust to Slice 1 either way, but it is sequenced after Slice 1 per the roadmap.

---

## Conventions locked for this slice (read before coding)

- **Folder id format:** `yyyymmddhhmmss-<sanitized-name>` — 14-digit creation timestamp, a single `-`, then the sanitized display name. Example: `20260530142233-untitled`.
- **Timestamp is injected.** `IdaProject::create (displayName, timestampDigits)` takes the 14-char `yyyymmddhhmmss` string from the caller. Production wires it from the wall clock at `MainComponent` construction (Slice 3); **tests pass a literal**, so every assertion is deterministic. There is **no** wall-clock call inside `core`.
- **Stable id vs display name (§2.2).** `folderId()` is set once at creation and is immutable. `displayName()` is a separate, mutable field; `setDisplayName()` changes **only** the display name and leaves `folderId()` byte-for-byte unchanged. Renaming never re-sanitizes or re-stamps the folder.
- **Tape stem:** `tape_<x>` where `x` is the **1-based `TapeId::value()`** (the spec's "1-based index"; `TapeId`s already start at 1 per Slice 1). The on-disk file keeps the established `.idatape` extension (the container is FLAC-or-PCM by tier, decided elsewhere by `codecForTier`); the builder owns the full filename `tape_<x>.idatape` so the three current call sites can later converge on one source of truth.
- **Sanitization rule (filesystem-safe, cross-platform):** lowercase ASCII letters/digits pass through; every other character — spaces, `/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|`, `.`, control chars, non-ASCII — is replaced with a single `_`; **runs of replaced characters collapse to one `_`**; leading/trailing `_` are trimmed; an empty or all-illegal name sanitizes to the literal `untitled`. Letters are lowercased so the folder is stable on case-insensitive (macOS default, Windows) and case-sensitive (Linux) filesystems alike. This is intentionally conservative — it never produces a Windows-reserved name fragment, a dot, or a path separator.
- **Home of the code:** pure logic + `IdaProject` → `core/include/ida/IdaProject.h` + `core/src/IdaProject.cpp` (JUCE-free). `juce::File` builders → `persistence/include/ida/ProjectPaths.h` + `persistence/src/ProjectPaths.cpp`. Test files → `tests/IdaProjectTests.cpp` (pure) + `tests/ProjectPathsTests.cpp` (file-path).

---

## Task 1 — `IdaProject` pure value type + folder-id naming (core, JUCE-free)

**Files:**
- Create: `core/include/ida/IdaProject.h`
- Create: `core/src/IdaProject.cpp`
- Create test: `tests/IdaProjectTests.cpp`
- Modify: `core/CMakeLists.txt` (add `src/IdaProject.cpp` to `IdaCore`)
- Modify: `tests/CMakeLists.txt` (add `IdaProjectTests.cpp` to `add_executable(IdaTests …)`)

- [ ] **Step 1: Register the new core source + test in CMake**

In `core/CMakeLists.txt`, add `src/IdaProject.cpp` to the `add_library(IdaCore STATIC …)` list (after `src/TapeRecord.cpp`).

In `tests/CMakeLists.txt`, add `IdaProjectTests.cpp` to the `add_executable(IdaTests …)` source list (place it on its own line after `TapePoolTests.cpp` on line 12 to keep tape-adjacent tests grouped).

- [ ] **Step 2: Write the failing pure-logic test `tests/IdaProjectTests.cpp`**

Real Catch2, no placeholders. Covers sanitization, folder-id format, the create/display split, and the two-same-named-projects-distinct-folders rule (driven by distinct injected timestamps).

```cpp
// Tests for ida::IdaProject — the project unit that owns its tapes (blank-slate
// first-run, Slice 2; spec §2.2). Pure / JUCE-free: the creation timestamp is
// INJECTED as a 14-char yyyymmddhhmmss string so every assertion is
// deterministic and no wall clock is ever read inside a test.
#include "ida/IdaProject.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using ida::IdaProject;

TEST_CASE ("IdaProject::create builds folderId = <timestamp>-<sanitized-name>", "[ida-project]")
{
    const auto p = IdaProject::create ("Untitled", "20260530142233");
    CHECK (p.displayName() == "Untitled");
    CHECK (p.createdTimestamp() == "20260530142233");
    CHECK (p.folderId() == "20260530142233-untitled");
}

TEST_CASE ("IdaProject sanitizes spaces and illegal characters to single underscores", "[ida-project]")
{
    CHECK (IdaProject::create ("My Song",        "20260101000000").folderId()
           == "20260101000000-my_song");
    CHECK (IdaProject::create ("Take 1/Final",   "20260101000000").folderId()
           == "20260101000000-take_1_final");
    CHECK (IdaProject::create ("a:b*c?d",        "20260101000000").folderId()
           == "20260101000000-a_b_c_d");
    // runs of illegal characters collapse to ONE underscore
    CHECK (IdaProject::create ("a   b",          "20260101000000").folderId()
           == "20260101000000-a_b");
    CHECK (IdaProject::create ("dots...here",    "20260101000000").folderId()
           == "20260101000000-dots_here");
}

TEST_CASE ("IdaProject trims leading/trailing underscores and lowercases", "[ida-project]")
{
    CHECK (IdaProject::create ("  Padded  ",  "20260101000000").folderId()
           == "20260101000000-padded");
    CHECK (IdaProject::create ("UPPER",       "20260101000000").folderId()
           == "20260101000000-upper");
    CHECK (IdaProject::create ("---x---",     "20260101000000").folderId()
           == "20260101000000-x");
}

TEST_CASE ("IdaProject with an empty or all-illegal name falls back to 'untitled'", "[ida-project]")
{
    CHECK (IdaProject::create ("",        "20260101000000").folderId()
           == "20260101000000-untitled");
    CHECK (IdaProject::create ("///",     "20260101000000").folderId()
           == "20260101000000-untitled");
    CHECK (IdaProject::create ("   ",     "20260101000000").folderId()
           == "20260101000000-untitled");
}

TEST_CASE ("IdaProject rename changes the display name only, never the folder", "[ida-project]")
{
    auto p = IdaProject::create ("Untitled", "20260530142233");
    const auto folderBefore = p.folderId();
    p.setDisplayName ("My Real Song Name");
    CHECK (p.displayName() == "My Real Song Name");
    CHECK (p.folderId()    == folderBefore);          // folder is immutable
    CHECK (p.folderId()    == "20260530142233-untitled");
}

TEST_CASE ("two same-named projects get distinct folders (timestamp disambiguates)", "[ida-project]")
{
    const auto a = IdaProject::create ("Untitled", "20260530142233");
    const auto b = IdaProject::create ("Untitled", "20260530142259");
    CHECK (a.folderId() != b.folderId());
    CHECK (a.folderId() == "20260530142233-untitled");
    CHECK (b.folderId() == "20260530142259-untitled");
}
```

- [ ] **Step 3: Run the test, verify it FAILS (does not compile — no header yet)**

Run: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests`
Expected: FAIL — compile error `fatal error: 'ida/IdaProject.h' file not found`.

- [ ] **Step 4: Write `core/include/ida/IdaProject.h`**

Real header, JUCE-free, mirrors the `TapeId`/`TapeDescriptor` style (explicit, const-correct, documented).

```cpp
#pragma once

#include <string>

namespace ida
{

/// The top-level persistence unit that owns an arrangement and ALL of its tapes
/// (spec §2.2). Tapes are project-scoped on disk; the project's `folderId` is the
/// grouper that holds them.
///
/// Two identities are deliberately decoupled:
///   * `folderId()` — `yyyymmddhhmmss-<sanitized-name>`, set ONCE at creation and
///     immutable. It is the on-disk grouper and the stable id; renaming a project
///     never changes it (avoids folder churn / broken tape paths).
///   * `displayName()` — the user-facing project name; mutable via `setDisplayName`.
///
/// JUCE-free by design (core/ is JUCE-free): the creation timestamp is supplied by
/// the caller as a 14-character `yyyymmddhhmmss` string, so this type is fully
/// deterministic and unit-testable without reading a wall clock. Production wires
/// the timestamp from the clock at construction time (the app layer).
class IdaProject
{
public:
    /// Builds a project. `displayName` is stored verbatim and ALSO sanitized into
    /// the folder id; `timestampDigits` must be the 14-char `yyyymmddhhmmss`
    /// creation stamp. The folder id is `timestampDigits + "-" + sanitize(displayName)`.
    /// An empty / all-illegal display name sanitizes to "untitled".
    static IdaProject create (std::string displayName, std::string timestampDigits);

    /// Sanitizes a display name to a filesystem-safe folder-name fragment:
    /// lowercase ASCII letters/digits pass; every other char becomes '_', runs of
    /// '_' collapse to one, leading/trailing '_' are trimmed; empty -> "untitled".
    /// Exposed for tests and for any caller that needs the fragment alone.
    static std::string sanitizeName (const std::string& displayName);

    const std::string& folderId() const noexcept         { return folderId_; }
    const std::string& displayName() const noexcept      { return displayName_; }
    const std::string& createdTimestamp() const noexcept { return createdTimestamp_; }

    /// Changes the user-facing name only. The folder id is NOT touched (§2.2).
    void setDisplayName (std::string displayName);

private:
    IdaProject (std::string folderId, std::string displayName, std::string createdTimestamp);

    std::string folderId_;
    std::string displayName_;
    std::string createdTimestamp_;
};

} // namespace ida
```

- [ ] **Step 5: Write `core/src/IdaProject.cpp`**

Real implementation. `sanitizeName` is a single-pass collapse; no magic numbers beyond the documented fallback literal.

```cpp
#include "ida/IdaProject.h"

#include <cctype>

namespace ida
{

namespace
{
    constexpr const char* kFallbackName = "untitled";

    bool isSafeNameChar (unsigned char c) noexcept
    {
        // Lowercased ASCII letter or digit. Everything else is replaced.
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }
}

std::string IdaProject::sanitizeName (const std::string& displayName)
{
    std::string out;
    out.reserve (displayName.size());

    bool pendingUnderscore = false;
    for (const char rawCh : displayName)
    {
        const auto lowered = static_cast<unsigned char> (
            std::tolower (static_cast<unsigned char> (rawCh)));

        if (isSafeNameChar (lowered))
        {
            // Emit a single collapsed '_' for any preceding run of illegal chars,
            // but never as a leading character.
            if (pendingUnderscore && ! out.empty())
                out.push_back ('_');
            pendingUnderscore = false;
            out.push_back (static_cast<char> (lowered));
        }
        else
        {
            pendingUnderscore = true; // defer — collapses runs and trims trailing
        }
    }

    if (out.empty())
        return kFallbackName;
    return out;
}

IdaProject IdaProject::create (std::string displayName, std::string timestampDigits)
{
    auto folderId = timestampDigits + "-" + sanitizeName (displayName);
    return IdaProject (std::move (folderId), std::move (displayName), std::move (timestampDigits));
}

IdaProject::IdaProject (std::string folderId, std::string displayName, std::string createdTimestamp)
    : folderId_ (std::move (folderId)),
      displayName_ (std::move (displayName)),
      createdTimestamp_ (std::move (createdTimestamp))
{
}

void IdaProject::setDisplayName (std::string displayName)
{
    displayName_ = std::move (displayName); // folderId_ deliberately untouched (§2.2)
}

} // namespace ida
```

- [ ] **Step 6: Run the test, verify it PASSES**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R ida-project`
Expected: PASS — all six `[ida-project]` cases green.

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/IdaProject.h core/src/IdaProject.cpp tests/IdaProjectTests.cpp core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: IdaProject unit — stable folderId vs display name + name sanitization (blank-slate Slice 2)"
```

---

## Task 2 — Project-scoped path builders (`projectTapesDir` + `tapeFileFor`) in persistence

**Files:**
- Create: `persistence/include/ida/ProjectPaths.h`
- Create: `persistence/src/ProjectPaths.cpp`
- Create test: `tests/ProjectPathsTests.cpp`
- Modify: `persistence/CMakeLists.txt` (add `src/ProjectPaths.cpp` to `IdaPersistence`)
- Modify: `tests/CMakeLists.txt` (add `ProjectPathsTests.cpp` to `add_executable(IdaTests …)`)

- [ ] **Step 1: Register the new persistence source + test in CMake**

In `persistence/CMakeLists.txt`, add `src/ProjectPaths.cpp` to `add_library(IdaPersistence STATIC …)` (after `src/TapeStore.cpp`). `IdaPersistence` already links `Ida::Core` PUBLIC, so `ida/IdaProject.h` resolves with no extra link line.

In `tests/CMakeLists.txt`, add `ProjectPathsTests.cpp` to the `add_executable(IdaTests …)` source list (next to `IdaProjectTests.cpp`).

- [ ] **Step 2: Write the failing path test `tests/ProjectPathsTests.cpp`**

Real Catch2. The `appSupportRoot` is **injected** as a `juce::File` (a temp dir) so the test never touches the real `~/Library`. Asserts the folder layout, the `tape_<x>.idatape` filename, the `TapeId`→stem mapping, and that no tape path escapes the project folder.

```cpp
// Tests for ida::persistence project-scoped tape paths (blank-slate Slice 2;
// spec §2.2/§2.1). The app-support root is INJECTED as a temp juce::File so the
// real ~/Library is never touched and assertions are hermetic.
#include "ida/ProjectPaths.h"

#include "ida/IdaProject.h"
#include "ida/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using ida::IdaProject;
using ida::TapeId;
using ida::persistence::projectTapesDir;
using ida::persistence::tapeFileFor;
using ida::persistence::tapeFileName;

TEST_CASE ("tapeFileName maps a 1-based TapeId to tape_<x>.idatape", "[project-paths]")
{
    CHECK (tapeFileName (TapeId (1)) == "tape_1.idatape");
    CHECK (tapeFileName (TapeId (2)) == "tape_2.idatape");
    CHECK (tapeFileName (TapeId (42)) == "tape_42.idatape");
}

TEST_CASE ("projectTapesDir nests the project folder under the app-support root", "[project-paths]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ida-projectpaths-test-root");
    const auto project = IdaProject::create ("Untitled", "20260530142233");

    const auto dir = projectTapesDir (root, project);
    CHECK (dir.getFileName() == "20260530142233-untitled");
    CHECK (dir.getParentDirectory() == root);
}

TEST_CASE ("tapeFileFor resolves <root>/<folderId>/tape_<x>.idatape", "[project-paths]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ida-projectpaths-test-root");
    const auto project = IdaProject::create ("My Song", "20260101000000");

    const auto file = tapeFileFor (root, project, TapeId (3));
    CHECK (file.getFileName() == "tape_3.idatape");
    CHECK (file.getParentDirectory().getFileName() == "20260101000000-my_song");
    // No tape path escapes the project folder (§2.1 no-orphan structural guard).
    CHECK (file.isAChildOf (projectTapesDir (root, project)));
    CHECK (file.isAChildOf (root));
}

TEST_CASE ("two same-named projects resolve to distinct tape paths", "[project-paths]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ida-projectpaths-test-root");
    const auto a = IdaProject::create ("Untitled", "20260530142233");
    const auto b = IdaProject::create ("Untitled", "20260530142259");

    CHECK (tapeFileFor (root, a, TapeId (1))
           != tapeFileFor (root, b, TapeId (1)));
}
```

- [ ] **Step 3: Run the test, verify it FAILS (no header yet)**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — compile error `fatal error: 'ida/ProjectPaths.h' file not found`.

- [ ] **Step 4: Write `persistence/include/ida/ProjectPaths.h`**

```cpp
#pragma once

#include "ida/IdaProject.h"
#include "ida/TapeId.h"

#include <juce_core/juce_core.h>

#include <string>

namespace ida::persistence
{

/// The on-disk file name for one tape: `tape_<x>.idatape`, where `x` is the
/// 1-based `TapeId::value()` (spec §2.2). The container format (FLAC or PCM) is
/// a tier decision made elsewhere; the extension stays `.idatape` so existing
/// record containers keep their identity. The grouper (project folder) carries
/// the project identity, so the file name itself stays minimal.
std::string tapeFileName (TapeId id);

/// The project's tape directory: `<appSupportRoot>/<project.folderId()>`. This is
/// the grouper folder that holds the project's tapes directly (spec §2.2,
/// replacing the flat `…/IDA/tapes/`). `appSupportRoot` is injected (the app
/// passes `idaAppSupportDirectory()`; tests pass a temp dir).
juce::File projectTapesDir (const juce::File& appSupportRoot, const IdaProject& project);

/// The fully-qualified path of one tape:
/// `<appSupportRoot>/<project.folderId()>/tape_<x>.idatape`. Every tape path is a
/// child of its project folder — no tape ever lives outside an owning project
/// (the structural expression of §2.1 "no orphan tapes").
juce::File tapeFileFor (const juce::File& appSupportRoot, const IdaProject& project, TapeId id);

} // namespace ida::persistence
```

- [ ] **Step 5: Write `persistence/src/ProjectPaths.cpp`**

```cpp
#include "ida/ProjectPaths.h"

namespace ida::persistence
{

namespace
{
    constexpr const char* kTapeFileExtension = ".idatape";
    constexpr const char* kTapeStemPrefix    = "tape_";
}

std::string tapeFileName (TapeId id)
{
    return std::string (kTapeStemPrefix) + std::to_string (id.value()) + kTapeFileExtension;
}

juce::File projectTapesDir (const juce::File& appSupportRoot, const IdaProject& project)
{
    return appSupportRoot.getChildFile (juce::String (project.folderId()));
}

juce::File tapeFileFor (const juce::File& appSupportRoot, const IdaProject& project, TapeId id)
{
    return projectTapesDir (appSupportRoot, project)
        .getChildFile (juce::String (tapeFileName (id)));
}

} // namespace ida::persistence
```

- [ ] **Step 6: Run the test, verify it PASSES**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R project-paths`
Expected: PASS — all four `[project-paths]` cases green.

- [ ] **Step 7: Commit**

```bash
git add persistence/include/ida/ProjectPaths.h persistence/src/ProjectPaths.cpp tests/ProjectPathsTests.cpp persistence/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: project-scoped tape path builder — <folderId>/tape_<x>.idatape, no orphan paths (blank-slate Slice 2)"
```

---

## Task 3 — Full-suite green + record the store-root migration for Slice 3/4

**Files:**
- Test: full `IdaTests` suite (no new file)
- Modify: `todo.md` (record the store-root rewiring that is intentionally NOT done in Slice 2)

This task adds **no production behavior change to the live store root** — Slice 2's contract is "the builder exists and resolves correct, collision-free, no-orphan paths." Rewiring `tapesDirectory()` / `TapeRecordWriter` / the two reader sites onto the builder is Slice 3/4 (boot path knows the live `IdaProject`; Slice 2 has no project instance at app scope yet). This task verifies Slice 2 broke nothing and records the deferred rewiring explicitly so it is not lost.

- [ ] **Step 1: Build and run the whole suite, verify PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build`
Expected: PASS at the project baseline (449/450; the single non-pass is the separately-run `MainComponentPluginEditorTests` exe, unrelated to this slice). The new `[ida-project]` and `[project-paths]` cases are included.

- [ ] **Step 2: Confirm the `IDA` app still links (no ripple)**

Run: `cmake --build build --target IDA`
Expected: links green. Slice 2 adds new files only and changes no existing signature, so there is no call-site ripple.

- [ ] **Step 3: Record the deferred store-root rewiring in `todo.md`**

Append (do not invent code — this is a real, bounded deferral that belongs to Slice 3/4, not Slice 2):

```
### 2026-05-30 - Slice 2 follow-on: route the live tape store through ProjectPaths
- Files: app/MainComponent.cpp (tapesDirectory() ~90, TapeRecordWriter ctor ~4263, reader sites ~6059/6081), audio/src/TapeRecordWriter.cpp:104 (tapeFile), audio/include/ida/TapeRecordWriter.h
- What was deferred: the live store root is still `…/IDA/tapes/` with `tape-<id>.idatape`; Slice 2 built `ida::persistence::tapeFileFor`/`projectTapesDir`/`tapeFileName` (`<folderId>/tape_<x>.idatape`) but did not rewire the three hardcoded sites.
- Why deferred: rewiring needs the live `IdaProject` instance, which is minted by the blank-slate boot path (Slice 3) / channel-create + capture wiring (Slice 4); Slice 2 has no app-scoped project to path against yet.
- What's needed to finish: in Slice 3/4, hold an `IdaProject` on MainComponent, pass `projectTapesDir(idaAppSupportDirectory(), project)` to the TapeRecordWriter ctor, and have TapeRecordWriter::tapeFile + the two MainComponent reader sites build their path via `ida::persistence::tapeFileName(id)` (one source of truth). Decide there whether the writer takes a pre-resolved tapesDir (current shape) or learns the builder.
```

- [ ] **Step 4: Commit**

```bash
git add todo.md
git commit -m "chore: record Slice 2 store-root rewiring deferral for Slice 3/4 (blank-slate)"
```

---

## Self-review

- **Spec coverage:**
  - §2.2 "project owns tapes; folder is the grouper named `yyyymmddhhmmss-<name>`" → Task 1 (`IdaProject::folderId()`), Task 2 (`projectTapesDir`).
  - §2.2 "tape files are simply `tape_<x>` (1-based)" → Task 2 (`tapeFileName` = `tape_<x>.idatape`, mapped from `TapeId::value()`).
  - §2.2 "fully-qualified name is folder + file" → Task 2 (`tapeFileFor` = `<root>/<folderId>/tape_<x>.idatape`).
  - §2.2 "folder is a stable creation-stamped id; display name is a separate field; rename changes display only, not the folder" → Task 1 (`setDisplayName` leaves `folderId_` untouched; explicit test).
  - §2.2 "timestamp disambiguates duplicate names (two `Untitled` never collide)" → Task 1 + Task 2 (distinct-folder / distinct-path tests).
  - §2.1 "no orphan tapes — every tape lives inside an owning project folder" → Task 2 (`isAChildOf(projectTapesDir)` / `isAChildOf(root)` structural guard; no builder produces a path outside the project folder).
  - Out of scope here (correctly): the deliberate-erase warning, New Song, demo retirement (Slice 3); recording-iff-assigned (Slice 4) — Slice 2 is storage + naming only, per the roadmap.
- **Deterministic-clock directive honored:** `IdaProject::create` takes the 14-char timestamp as a parameter; **no** `Time`/wall-clock call exists in `core` or in any test. Production injects the real clock at the app layer (Slice 3).
- **Placeholder scan:** no `TBD`/`TODO`/`FIXME`/`stub`/`placeholder`/"handle errors"/"similar to above" in any code step — every header, source, and test is written out in full. The one `todo.md` entry is a genuine, bounded cross-slice deferral (store-root rewiring needs Slice 3/4's live project), per the "No Silent Deferral" rule — not a dodge of in-scope work.
- **Names/types consistent with Slice 1 + the naming convention:** `TapeId` is consumed via its existing `value()`/`explicit ctor` (unchanged by Slice 1); Slice 2 does not call `primary()`, so it is independent of Slice 1's `std::optional<TapeId>` change. `IdaProject` lives in `namespace ida` (core) and the path builders in `namespace ida::persistence`, matching `TapePool`/`TapeStore`/`SessionFormat` placement. The single naming convention — folder `yyyymmddhhmmss-<sanitized-name>`, file `tape_<x>.idatape` — is centralized in two functions (`IdaProject::create`, `tapeFileName`) so Slices 3/4/7 reference one source of truth.
- **Layer discipline:** pure, JUCE-free naming logic stays in `core` (testable per `core/CMakeLists.txt`'s "do NOT add JUCE" rule); only the `juce::File` builders live in `persistence`. The split matches the roadmap's "new IdaProject type (core/persistence)".
