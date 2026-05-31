# Tape Subsystem — Slice 1: Tape Pool Model + Persistence — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a JUCE-free project tape pool (`TapeDescriptor` + `TapePool`, ≥1 tape, unbounded, monotonic ids) in `core`, plus its `SessionFormat` round-trip in `persistence`, fully headless-TDD'd.

**Architecture:** Mirrors the established `MixerGraphState` (core POD type) + `serializeMixerGraphState` (persistence serializer) layering. `TapePool` is the single source of truth for which tapes exist; later slices (multi-tape routing engine, capture wiring, Tapes UI) read it. This slice ships the tested type + serializer only — no `MixerGraph`, `TapeWriter`, UI, or `MainComponent` wiring (those are slices 2–4 per `docs/superpowers/specs/2026-05-21-tape-subsystem-design.md`).

**Tech Stack:** C++20, Catch2 (`IdaTests`), JUCE `juce_core` (`juce::var`/`juce::JSON`, persistence target only), CMake + Ninja.

**Design doc:** `docs/superpowers/specs/2026-05-21-tape-subsystem-design.md` (read "Slice 1 detail").

---

## File Structure

- **Create** `core/include/ida/TapeDescriptor.h` — value-typed pool entry `{ TapeId id; std::string name; }` + `operator==`. JUCE-free.
- **Create** `core/include/ida/TapePool.h` — the `TapePool` class declaration.
- **Create** `core/src/TapePool.cpp` — its implementation.
- **Modify** `core/CMakeLists.txt:25` — add `src/TapePool.cpp` to the `IdaCore` source list.
- **Modify** `persistence/include/ida/SessionFormat.h` — declare `serializeTapePool` / `deserializeTapePool`.
- **Modify** `persistence/src/SessionFormat.cpp` — define them, reusing the file's anon-namespace helpers.
- **Create** `tests/TapePoolTests.cpp` — `[tape-pool]` logic cases + `[tape-pool][sessionformat]` round-trip cases (IdaTests links both `Ida::Core` and `Ida::Persistence`, so both live in one file).
- **Modify** `tests/CMakeLists.txt` — add `TapePoolTests.cpp` to the `IdaTests` source list.

Established idioms confirmed in the tree (use these verbatim):
- Catch2 header: `#include <catch2/catch_test_macros.hpp>`; macros `TEST_CASE`, `SECTION`, `CHECK`, `REQUIRE`, `CHECK_FALSE`, `REQUIRE_THROWS_AS`.
- `TapeId` is `explicit constexpr TapeId(std::int64_t)`, `.value()`, `operator==`/`!=`.
- SessionFormat.cpp anon-namespace helpers: `fail(msg)` (throws `std::runtime_error` prefixed `ida::persistence::SessionFormat: `), `makeObject()` → `juce::DynamicObject::Ptr`, `objectVar(ptr)`, `requireProperty(var,name)`, `optionalProperty(var,name)`, `requireInt64(var,context)`, `requireString`/`.toString()`, `juce::Array<juce::var>`, `juce::JSON::toString(objectVar(root))`, `juce::JSON::parse(json, out)`.

---

## Task 1: TapePool default construction + accessors

**Files:**
- Create: `core/include/ida/TapeDescriptor.h`
- Create: `core/include/ida/TapePool.h`
- Create: `core/src/TapePool.cpp`
- Modify: `core/CMakeLists.txt:25`
- Create: `tests/TapePoolTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/TapePoolTests.cpp`:

```cpp
// Tests for ida::TapePool — the project's pool of tapes (tape subsystem
// slice 1). Pins the >=1 invariant, monotonic id allocation, add/remove/rename,
// the explicit-list ctor's validation, and the SessionFormat round-trip.
#include "ida/TapePool.h"

#include "ida/SessionFormat.h"
#include "ida/TapeDescriptor.h"
#include "ida/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using ida::TapeDescriptor;
using ida::TapeId;
using ida::TapePool;

TEST_CASE ("default TapePool holds exactly one primary tape", "[tape-pool]")
{
    TapePool pool;
    REQUIRE (pool.count() == 1);
    CHECK (pool.at (0).id == TapeId (1));
    CHECK (pool.at (0).name == "Tape 1");
    CHECK (pool.primary() == TapeId (1));
    CHECK (pool.find (TapeId (1)) != nullptr);
    CHECK (pool.find (TapeId (999)) == nullptr);
    CHECK (pool.tapes().size() == 1u);
}
```

- [ ] **Step 2: Add the new test source to CMake**

In `tests/CMakeLists.txt`, add `TapePoolTests.cpp` to the `IdaTests` source list (the `add_executable(IdaTests ...)` block beginning at line 1). Insert it alphabetically near the other core tests, e.g. directly after the line `CaptureSessionTests.cpp`:

```cmake
    CaptureSessionTests.cpp
    TapePoolTests.cpp
```

- [ ] **Step 3: Run test to verify it fails (no TapePool yet)**

Run: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests`
Expected: FAIL — compile error, `sirius/TapePool.h` not found / `TapePool` undefined.

- [ ] **Step 4: Create `TapeDescriptor.h`**

Create `core/include/ida/TapeDescriptor.h`:

```cpp
#pragma once

#include "ida/TapeId.h"

#include <string>

namespace ida
{

/// One entry in the project tape pool: light metadata naming a tape that exists
/// as a capture destination. Parallel to InputDescriptor — honors the white
/// paper section 7.2 data-layer / structure-layer split: the heavy Tape<Payload>
/// stream does not know about descriptors; this points at a tape by id and
/// gives it a name.
struct TapeDescriptor
{
    TapeId      id;
    std::string name;

    bool operator== (const TapeDescriptor& other) const noexcept
    {
        return id == other.id && name == other.name;
    }
};

} // namespace ida
```

- [ ] **Step 5: Create `TapePool.h`**

Create `core/include/ida/TapePool.h`:

```cpp
#pragma once

#include "ida/TapeDescriptor.h"
#include "ida/TapeId.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ida
{

/// The project's pool of tapes — an ordered list, minimum one, unbounded
/// maximum. The single source of truth for "which tapes exist"; the timeline,
/// the Tapes tab, and the input mixer's routing all read it. Message-thread only
/// (no audio-thread access in this slice).
class TapePool
{
public:
    /// Seeds exactly one tape (TapeId{1}, "Tape 1") so the >=1 invariant holds
    /// from construction.
    TapePool();

    /// Constructs from an explicit list (used by deserialization). The list must
    /// be non-empty and its ids unique; nextId_ is seeded one past the max id.
    /// Throws std::invalid_argument on an empty list or duplicate ids.
    explicit TapePool (std::vector<TapeDescriptor> tapes);

    /// Appends a new tape with the given name and a freshly allocated id;
    /// returns the new id.
    TapeId add (std::string name);

    /// Removes the tape with the given id. Returns false (no change) if the id
    /// is unknown OR if removing it would leave the pool empty (the >=1 floor).
    bool remove (TapeId id);

    /// Renames the tape with the given id. Returns false if the id is unknown.
    bool rename (TapeId id, std::string name);

    int                                count() const noexcept;
    const TapeDescriptor*              find (TapeId id) const noexcept; // nullptr if absent
    const TapeDescriptor&              at (int index) const;            // throws std::out_of_range
    const std::vector<TapeDescriptor>& tapes() const noexcept;

    /// The primary tape — the first entry; always valid (>=1 invariant).
    TapeId primary() const noexcept;

private:
    std::vector<TapeDescriptor> tapes_;
    std::int64_t                nextId_ { 1 };
};

} // namespace ida
```

- [ ] **Step 6: Create `TapePool.cpp` (default ctor + accessors only for now)**

Create `core/src/TapePool.cpp`:

```cpp
#include "ida/TapePool.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ida
{

TapePool::TapePool()
{
    tapes_.push_back (TapeDescriptor { TapeId (1), "Tape 1" });
    nextId_ = 2;
}

int TapePool::count() const noexcept { return static_cast<int> (tapes_.size()); }

const std::vector<TapeDescriptor>& TapePool::tapes() const noexcept { return tapes_; }

TapeId TapePool::primary() const noexcept { return tapes_.front().id; }

const TapeDescriptor* TapePool::find (TapeId id) const noexcept
{
    for (const auto& t : tapes_)
        if (t.id == id)
            return &t;
    return nullptr;
}

const TapeDescriptor& TapePool::at (int index) const
{
    return tapes_.at (static_cast<std::size_t> (index));
}

} // namespace ida
```

- [ ] **Step 7: Add `TapePool.cpp` to `core/CMakeLists.txt`**

Modify line 25 of `core/CMakeLists.txt` — change `src/VersionPinningRecord.cpp)` to add the new source before the closing paren:

```cmake
    src/VersionPinningRecord.cpp
    src/TapePool.cpp)
```

- [ ] **Step 8: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[tape-pool]"`
Expected: PASS — 1 test case, all assertions pass.

- [ ] **Step 9: Commit**

```bash
git add core/include/ida/TapeDescriptor.h core/include/ida/TapePool.h core/src/TapePool.cpp core/CMakeLists.txt tests/TapePoolTests.cpp tests/CMakeLists.txt
git commit -m "feat: TapePool default construction + accessors (tape subsystem slice 1)"
```

---

## Task 2: TapePool::add appends with a monotonic id

**Files:**
- Modify: `core/src/TapePool.cpp`
- Test: `tests/TapePoolTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePoolTests.cpp`:

```cpp
TEST_CASE ("TapePool::add appends a tape with a fresh monotonic id", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("Drums");
    const auto b = pool.add ("Bass");

    CHECK (a == TapeId (2));
    CHECK (b == TapeId (3));
    REQUIRE (pool.count() == 3);
    CHECK (pool.at (1).id == a);
    CHECK (pool.at (1).name == "Drums");
    CHECK (pool.at (2).id == b);
    CHECK (pool.at (2).name == "Bass");
    CHECK (pool.primary() == TapeId (1)); // primary unchanged by add
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — link error, `TapePool::add` undefined.

- [ ] **Step 3: Implement `add`**

Add to `core/src/TapePool.cpp` (inside `namespace ida`, after `at`):

```cpp
TapeId TapePool::add (std::string name)
{
    const TapeId id (nextId_++);
    tapes_.push_back (TapeDescriptor { id, std::move (name) });
    return id;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[tape-pool]"`
Expected: PASS — 2 test cases.

- [ ] **Step 5: Commit**

```bash
git add core/src/TapePool.cpp tests/TapePoolTests.cpp
git commit -m "feat: TapePool::add with monotonic id allocation"
```

---

## Task 3: TapePool::remove with the >=1 floor and unknown-id refusal

**Files:**
- Modify: `core/src/TapePool.cpp`
- Test: `tests/TapePoolTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePoolTests.cpp`:

```cpp
TEST_CASE ("TapePool::remove erases a tape but enforces the >=1 floor", "[tape-pool]")
{
    TapePool pool;
    const auto drums = pool.add ("Drums");
    REQUIRE (pool.count() == 2);

    SECTION ("removing a non-last tape succeeds")
    {
        CHECK (pool.remove (drums));
        CHECK (pool.count() == 1);
        CHECK (pool.find (drums) == nullptr);
        CHECK (pool.primary() == TapeId (1));
    }

    SECTION ("removing the last remaining tape is refused (floor of 1)")
    {
        REQUIRE (pool.remove (drums));        // back down to 1
        REQUIRE (pool.count() == 1);
        CHECK_FALSE (pool.remove (TapeId (1))); // refused
        CHECK (pool.count() == 1);              // unchanged — falsifiable
        CHECK (pool.find (TapeId (1)) != nullptr);
    }

    SECTION ("removing an unknown id returns false")
    {
        CHECK_FALSE (pool.remove (TapeId (999)));
        CHECK (pool.count() == 2);
    }
}

TEST_CASE ("TapePool::add after remove never reuses an id", "[tape-pool]")
{
    TapePool pool;
    const auto a = pool.add ("A"); // id 2
    REQUIRE (pool.remove (a));     // erase id 2
    const auto b = pool.add ("B"); // must be id 3, not 2
    CHECK (b == TapeId (3));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — link error, `TapePool::remove` undefined.

- [ ] **Step 3: Implement `remove`**

Add to `core/src/TapePool.cpp`:

```cpp
bool TapePool::remove (TapeId id)
{
    if (tapes_.size() <= 1) // >=1 floor: never empty the pool
        return false;

    const auto it = std::find_if (tapes_.begin(), tapes_.end(),
                                  [id] (const TapeDescriptor& t) { return t.id == id; });
    if (it == tapes_.end())
        return false;

    tapes_.erase (it);
    return true;
}
```

Note: `nextId_` is never decremented, so ids are never reused (proven by the second test case).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[tape-pool]"`
Expected: PASS — 4 test cases.

- [ ] **Step 5: Commit**

```bash
git add core/src/TapePool.cpp tests/TapePoolTests.cpp
git commit -m "feat: TapePool::remove with >=1 floor and no id reuse"
```

---

## Task 4: TapePool::rename

**Files:**
- Modify: `core/src/TapePool.cpp`
- Test: `tests/TapePoolTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePoolTests.cpp`:

```cpp
TEST_CASE ("TapePool::rename changes a tape's name", "[tape-pool]")
{
    TapePool pool;
    const auto drums = pool.add ("Drms"); // typo

    CHECK (pool.rename (drums, "Drums"));
    CHECK (pool.find (drums)->name == "Drums");

    CHECK_FALSE (pool.rename (TapeId (999), "Nope")); // unknown id
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — link error, `TapePool::rename` undefined.

- [ ] **Step 3: Implement `rename`**

Add to `core/src/TapePool.cpp`:

```cpp
bool TapePool::rename (TapeId id, std::string name)
{
    for (auto& t : tapes_)
    {
        if (t.id == id)
        {
            t.name = std::move (name);
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[tape-pool]"`
Expected: PASS — 5 test cases.

- [ ] **Step 5: Commit**

```bash
git add core/src/TapePool.cpp tests/TapePoolTests.cpp
git commit -m "feat: TapePool::rename"
```

---

## Task 5: Explicit-list constructor (validation + nextId seeding)

**Files:**
- Modify: `core/src/TapePool.cpp`
- Test: `tests/TapePoolTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePoolTests.cpp`:

```cpp
TEST_CASE ("TapePool explicit-list ctor seeds from a non-empty unique list", "[tape-pool]")
{
    TapePool pool (std::vector<TapeDescriptor> {
        TapeDescriptor { TapeId (4), "Vox" },
        TapeDescriptor { TapeId (7), "Gtr" } });

    REQUIRE (pool.count() == 2);
    CHECK (pool.primary() == TapeId (4));
    CHECK (pool.at (1).name == "Gtr");

    // nextId_ seeded one past the max id (7) -> next add is 8.
    CHECK (pool.add ("New") == TapeId (8));
}

TEST_CASE ("TapePool explicit-list ctor rejects empty and duplicate ids", "[tape-pool]")
{
    REQUIRE_THROWS_AS (TapePool (std::vector<TapeDescriptor> {}),
                       std::invalid_argument);

    REQUIRE_THROWS_AS (
        TapePool (std::vector<TapeDescriptor> {
            TapeDescriptor { TapeId (3), "A" },
            TapeDescriptor { TapeId (3), "B" } }),
        std::invalid_argument);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — link error, the explicit-list `TapePool` ctor is undefined.

- [ ] **Step 3: Implement the explicit-list ctor**

Add to `core/src/TapePool.cpp`:

```cpp
TapePool::TapePool (std::vector<TapeDescriptor> tapes)
    : tapes_ (std::move (tapes))
{
    if (tapes_.empty())
        throw std::invalid_argument ("ida::TapePool: tape list must be non-empty (>=1 invariant)");

    std::unordered_set<std::int64_t> seen;
    std::int64_t maxId = 0;
    for (const auto& t : tapes_)
    {
        if (! seen.insert (t.id.value()).second)
            throw std::invalid_argument ("ida::TapePool: duplicate tape id");
        maxId = std::max (maxId, t.id.value());
    }
    nextId_ = maxId + 1;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[tape-pool]"`
Expected: PASS — 7 test cases.

- [ ] **Step 5: Commit**

```bash
git add core/src/TapePool.cpp tests/TapePoolTests.cpp
git commit -m "feat: TapePool explicit-list ctor with validation + nextId seeding"
```

---

## Task 6: SessionFormat tape-pool round-trip

**Files:**
- Modify: `persistence/include/ida/SessionFormat.h`
- Modify: `persistence/src/SessionFormat.cpp`
- Test: `tests/TapePoolTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/TapePoolTests.cpp`:

```cpp
TEST_CASE ("TapePool round-trips through SessionFormat", "[tape-pool][sessionformat]")
{
    TapePool original (std::vector<TapeDescriptor> {
        TapeDescriptor { TapeId (1), "Tape 1" },
        TapeDescriptor { TapeId (5), "Drums" },
        TapeDescriptor { TapeId (9), "Bass" } });

    const auto json   = ida::persistence::serializeTapePool (original);
    const auto loaded = ida::persistence::deserializeTapePool (json);

    REQUIRE (loaded.count() == original.count());
    for (int i = 0; i < original.count(); ++i)
        CHECK (loaded.at (i) == original.at (i));

    // nextId_ survives: next add is one past the max imported id (9) -> 10.
    auto loadedCopy = loaded;
    CHECK (loadedCopy.add ("New") == TapeId (10));
}

TEST_CASE ("deserializeTapePool rejects empty and malformed documents", "[tape-pool][sessionformat]")
{
    // Present-but-empty tapes array violates the >=1 on-disk contract.
    REQUIRE_THROWS_AS (ida::persistence::deserializeTapePool ("{ \"tapes\": [] }"),
                       std::runtime_error);
    // Not valid JSON.
    REQUIRE_THROWS_AS (ida::persistence::deserializeTapePool ("{ not json"),
                       std::runtime_error);
    // Object without a tapes array.
    REQUIRE_THROWS_AS (ida::persistence::deserializeTapePool ("{}"),
                       std::runtime_error);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target IdaTests`
Expected: FAIL — compile/link error, `serializeTapePool` / `deserializeTapePool` undefined.

- [ ] **Step 3: Declare the functions in `SessionFormat.h`**

In `persistence/include/ida/SessionFormat.h`, add the include and declarations. After the existing `#include "ida/MixerGraphState.h"` line, add:

```cpp
#include "ida/TapePool.h"
```

Then, before the closing `} // namespace ida::persistence` (after the `deserializeOutputMixerGraphState` declaration), add:

```cpp
/// Serializes the project tape pool to a self-contained JSON document. Round-
/// trips exactly through deserializeTapePool. Independent of the Constituent
/// session document and the mixer-graph documents.
juce::String serializeTapePool (const TapePool& pool);

/// Reconstructs a tape pool from serializeTapePool's output. Throws
/// std::runtime_error on a malformed document. A present-but-empty tapes array
/// is rejected (the >=1 invariant is a load-time contract too). Callers loading
/// a pre-tape-pool session construct a default TapePool() instead of calling
/// this (forward-compat is the caller's responsibility, matching the mixer-graph
/// convention).
TapePool deserializeTapePool (const juce::String& json);
```

- [ ] **Step 4: Define the functions in `SessionFormat.cpp`**

In `persistence/src/SessionFormat.cpp`, add the include near the top with the other `sirius/` includes:

```cpp
#include "ida/TapePool.h"
```

Then add the two function definitions inside `namespace ida::persistence`, immediately before the closing `} // namespace ida::persistence` (after `deserializeOutputMixerGraphState`). These reuse the file's existing anon-namespace helpers (`makeObject`, `objectVar`, `requireProperty`, `requireInt64`, `fail`):

```cpp
juce::String serializeTapePool (const TapePool& pool)
{
    juce::Array<juce::var> tapeArr;
    for (const auto& t : pool.tapes())
    {
        auto obj = makeObject();
        obj->setProperty ("id",   juce::int64 (t.id.value()));
        obj->setProperty ("name", juce::String (t.name));
        tapeArr.add (objectVar (obj));
    }
    auto root = makeObject();
    root->setProperty ("tapes", tapeArr);
    return juce::JSON::toString (objectVar (root));
}

TapePool deserializeTapePool (const juce::String& json)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);
    if (result.failed())
        fail ("invalid tape pool JSON: " + result.getErrorMessage().toStdString());
    if (! parsed.isObject())
        fail ("tape pool document must be a JSON object");

    const auto tapes = requireProperty (parsed, "tapes");
    if (! tapes.isArray() || tapes.size() == 0)
        fail ("tape pool must carry a non-empty tapes array");

    std::vector<TapeDescriptor> descriptors;
    descriptors.reserve (static_cast<std::size_t> (tapes.size()));
    for (int i = 0; i < tapes.size(); ++i)
    {
        const auto& entry = tapes[i];
        descriptors.push_back (TapeDescriptor {
            TapeId (requireInt64 (requireProperty (entry, "id"), "tape.id")),
            requireProperty (entry, "name").toString().toStdString() });
    }
    return TapePool (std::move (descriptors));
}
```

Note: `requireProperty` throws (via `fail`) on a missing key, and the `TapePool` explicit-list ctor throws `std::invalid_argument` on duplicate ids — but `deserializeTapePool`'s own contract is `std::runtime_error`. Duplicate-id JSON is out of this slice's test scope; if a later slice needs duplicate-id JSON to surface as `runtime_error`, wrap the ctor call. For now the non-empty check covers the tested contract.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target IdaTests && build/tests/IdaTests "[tape-pool]"`
Expected: PASS — 9 test cases total (`[tape-pool]` 7 + `[tape-pool][sessionformat]` 2).

- [ ] **Step 6: Commit**

```bash
git add persistence/include/ida/SessionFormat.h persistence/src/SessionFormat.cpp tests/TapePoolTests.cpp
git commit -m "feat: SessionFormat tape-pool round-trip (serialize/deserialize)"
```

---

## Task 7: Full-suite regression + push

**Files:** none (verification only).

- [ ] **Step 1: Clean rebuild to catch CMake/stale-config issues**

Run: `rm -rf build && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target IdaTests`
Expected: builds clean, no warnings-as-errors.

- [ ] **Step 2: Run the full test suite**

Run: `ctest --test-dir build`
Expected: the prior baseline + the new `TapePoolTests` cases, all passing except the documented `MainComponentPluginEditorTests_NOT_BUILT` sentinel (run separately by `bash/test-s7.sh`). Record the new count (was 531/532 at the bus-controls slice).

- [ ] **Step 3: Push**

```bash
git push origin master
```

- [ ] **Step 4: Update `continue.md`**

Refresh the RESUME-HERE block: slice 1 shipped (commits + new ctest count + clean-rebuild status); next = **slice 2 (multi-tape routing engine)** with its first concrete moves (brainstorm not needed — design doc `2026-05-21-tape-subsystem-design.md` slice 2 is the source; go writing-plans → subagent-driven-development). Note slices 3 (capture-to-disk, "real recording" — explicitly in the path per the operator) and 4 (Tapes UI) follow, then the original mixer Phase 6 resumes.

---

## Self-Review

**Spec coverage:** Every item in the design doc's "Slice 1 detail" maps to a task — `TapeDescriptor` (Task 1), `TapePool` ctor/accessors (1), `add` (2), `remove`+floor (3), `rename` (4), explicit-list ctor + validation + nextId seeding (5), persistence round-trip + strict empty/malformed (6), regression+push+handoff (7). Out-of-scope items (MixerGraph, TapeWriter, UI, MainComponent wiring) are explicitly excluded.

**Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to" — every code step shows complete code; every run step shows the exact command and expected result.

**Type consistency:** `TapeDescriptor { TapeId id; std::string name; }`, `TapePool` method names (`add`/`remove`/`rename`/`count`/`find`/`at`/`tapes`/`primary`), `nextId_` semantics, and the JSON shape (`{ "tapes": [ { "id", "name" } ] }`) are identical across the design doc, the test code, and the implementation across all tasks. `serializeTapePool`/`deserializeTapePool` names match between header (Task 6 Step 3), impl (Step 4), and tests (Step 1).
