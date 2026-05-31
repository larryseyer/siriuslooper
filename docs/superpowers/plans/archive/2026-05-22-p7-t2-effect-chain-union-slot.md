# P7 T2 — EffectChain Union Slot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Widen `EffectChainEntry` from a plugin-only descriptor to a tagged-union slot type `SlotKind = Empty | Internal | Plugin`, add a strongly-typed `InternalFxId` for the four built-in FX (EQ / CMP / RVB / DLY), and extend persistence round-trip with forward-compatible defaults (missing `kind` → `Plugin`). 8-slot cap and copy-on-write semantics unchanged. Headless TDD only — no UI work in this slice.

**Architecture:** A new strongly-typed `enum class InternalFxId` in its own header pins the four built-in FX as first-class values (independent of any descriptor / config payload — those land in T3 adapters). `EffectChainEntry` grows two fields: a `kind` discriminant defaulting to `Empty` (so an uninitialized entry never accidentally pretends to be a `Plugin`) and an `internalId` payload (meaningful only when `kind == Internal`). Two factory functions — `EffectChainEntry::makePlugin(...)` and `::makeInternal(...)` — replace ad-hoc field assignment at the construction site. Persistence emits a `kind` string discriminant, conditionally emits the plugin block vs the internal id, and back-loads pre-union session JSON (no `kind` field) as `Plugin` for forward compatibility.

**Tech Stack:** C++17, Catch2 v3, JUCE (`juce::var` for persistence), CMake + Ninja, `ctest`.

**Contract reference:** `docs/design/ida-internal-fx.md` "Union slot type contract" — that doc is normative; this plan implements it.

---

## File structure

**Create:**
- `core/include/ida/InternalFxId.h` — `enum class InternalFxId : uint8_t` + free `toString` / `fromString` helpers.
- `tests/InternalFxIdTests.cpp` — pin the enum values + the to/from-string round-trip.

**Modify:**
- `core/include/ida/EffectChain.h` — add `EffectChainSlotKind` enum, add `kind` + `internalId` fields to `EffectChainEntry`, add `makePlugin` / `makeInternal` factory static members, update `operator==`.
- `core/src/EffectChain.cpp` — implement the factory statics (one-line each; declaration-in-header would be fine too — pick whichever matches existing house style; the existing `EffectChain` class methods are out-of-line, so factories go out-of-line as well).
- `core/CMakeLists.txt` — add `InternalFxId.h` to the install / header list if the file enumerates headers explicitly (check; many CMake setups auto-glob).
- `persistence/src/SessionFormat.cpp` — extend `effectChainEntryToVar` (lines 549-560) + `effectChainEntryFromVar` (lines 562-585) to emit / read the `kind` discriminant + the `internalId` field. Forward-compat: missing `kind` defaults to `Plugin`.
- `tests/EffectChainTests.cpp` — update the `makeEntry` helper (lines 29-42) to call `EffectChainEntry::makePlugin(...)` instead of raw field assignment; add new test cases for the union (Internal entry construction, Internal round-trip, mixed chain, forward-compat back-load).
- `tests/MixerGraphPersistenceTests.cpp` — add Internal-FX insert round-trip cases on both InputMixer and OutputMixer.
- `tests/CMakeLists.txt` — register `InternalFxIdTests.cpp` in the test executable source list.

**Out of scope (T3 and later):** runtime adapter classes, OTTO Player FX wrappers, RT-safe param swap, any `engine/src/fx/` files, any UI surface. Nothing in `app/`, `ui/`, `engine/include/ida/InputMixer.h` interface changes, `MixerGraphState.h` (its `inserts` field is already an `EffectChain` and inherits the new shape for free).

---

## Task ordering rationale

Each task ends green-on-ctest with one focused commit. The order:
1. **Task 1** lands `InternalFxId` standalone — pure value type, no dependency on `EffectChainEntry`, can be tested in isolation.
2. **Task 2** extends `EffectChainEntry` with the union surface (no persistence yet). Existing engine code keeps compiling because the new fields default in a backwards-compatible way (`kind = Empty` is a safe default; existing test fixtures migrate to `makePlugin`).
3. **Task 3** wires persistence — separated from Task 2 so the persistence change can be reviewed independently and so a mid-plan halt still leaves a coherent engine.
4. **Task 4** is the mixer-graph integration test layer — proves channels and buses persist Internal inserts end-to-end through `InputMixer::exportGraphState` / `importGraphState` and round-trip via the mixer-graph JSON format.

The 8-slot cap and copy-on-write semantics are invariant across all four tasks — the existing tests guard them, and no task modifies the cap or COW behaviour.

---

### Task 1: InternalFxId header + tests

**Files:**
- Create: `core/include/ida/InternalFxId.h`
- Create: `tests/InternalFxIdTests.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source)

- [ ] **Step 1: Write the failing test**

Create `tests/InternalFxIdTests.cpp`:

```cpp
// Pins the four built-in FX ids + the round-trip string discriminant used by
// SessionFormat. The enum is the wire-stable identity of an Internal slot —
// renaming a value would break every saved session that contains an Internal
// effect, so these tests serve as the contract for both readers and writers.
#include "ida/InternalFxId.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

using ida::InternalFxId;

TEST_CASE ("InternalFxId enum values are stable", "[internal-fx-id]")
{
    // The underlying type is uint8_t with a reserved range for future FX.
    // Values are pinned because they appear (as strings) in saved sessions.
    CHECK (static_cast<std::uint8_t> (InternalFxId::kEq)  == 0);
    CHECK (static_cast<std::uint8_t> (InternalFxId::kCmp) == 1);
    CHECK (static_cast<std::uint8_t> (InternalFxId::kRvb) == 2);
    CHECK (static_cast<std::uint8_t> (InternalFxId::kDly) == 3);
}

TEST_CASE ("InternalFxId round-trips through toString / fromString", "[internal-fx-id]")
{
    for (auto id : { InternalFxId::kEq, InternalFxId::kCmp,
                     InternalFxId::kRvb, InternalFxId::kDly })
    {
        CHECK (ida::internalFxIdFromString (ida::internalFxIdToString (id)) == id);
    }
}

TEST_CASE ("internalFxIdToString produces the documented strings", "[internal-fx-id]")
{
    // These strings are the wire format. Do not rename them without a
    // forward-compat plan in SessionFormat.
    CHECK (ida::internalFxIdToString (InternalFxId::kEq)  == std::string ("EQ"));
    CHECK (ida::internalFxIdToString (InternalFxId::kCmp) == std::string ("CMP"));
    CHECK (ida::internalFxIdToString (InternalFxId::kRvb) == std::string ("RVB"));
    CHECK (ida::internalFxIdToString (InternalFxId::kDly) == std::string ("DLY"));
}

TEST_CASE ("internalFxIdFromString throws on unknown id", "[internal-fx-id]")
{
    CHECK_THROWS_AS (ida::internalFxIdFromString ("Synth"),  std::invalid_argument);
    CHECK_THROWS_AS (ida::internalFxIdFromString (""),       std::invalid_argument);
}
```

- [ ] **Step 2: Register the new test in CMake**

Edit `tests/CMakeLists.txt` — find the list of test source files (search for one of the existing tests, e.g. `EffectChainTests.cpp`, to locate the list) and add `InternalFxIdTests.cpp` alphabetically adjacent to it.

- [ ] **Step 3: Run test to verify it fails (header missing)**

Run:
```bash
cmake --build build --target IdaTests
```
Expected: FAIL with `'sirius/InternalFxId.h' file not found`.

- [ ] **Step 4: Create the header**

Create `core/include/ida/InternalFxId.h`:

```cpp
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ida
{

/// The four built-in FX shipped by IDA (white paper §6.6 + the contract in
/// `docs/design/ida-internal-fx.md`). Each id resolves at T3 to one of OTTO's
/// header-only Player FX via a IDA-side adapter. The underlying type is
/// `uint8_t` with a reserved range up to 16 so a future built-in (e.g. saturator,
/// transient shaper) can land without growing the discriminant.
///
/// These values are wire-stable — they ride inside session JSON as strings
/// (`internalFxIdToString` is the serialization, `internalFxIdFromString` the
/// deserialization). Renaming a value breaks every saved session that holds an
/// Internal slot with that id.
enum class InternalFxId : std::uint8_t
{
    kEq  = 0,
    kCmp = 1,
    kRvb = 2,
    kDly = 3,
    // reserved up to 15 for future built-ins
};

/// Wire-stable string form. Used by SessionFormat and by any other code that
/// needs to serialize / log an Internal id. The strings are the public ones
/// the operator sees on insert pickers (UI lookup is allowed to render a
/// friendlier label, but the canonical id stays).
inline std::string internalFxIdToString (InternalFxId id)
{
    switch (id)
    {
        case InternalFxId::kEq:  return "EQ";
        case InternalFxId::kCmp: return "CMP";
        case InternalFxId::kRvb: return "RVB";
        case InternalFxId::kDly: return "DLY";
    }
    throw std::invalid_argument ("ida::internalFxIdToString: unknown InternalFxId");
}

inline InternalFxId internalFxIdFromString (const std::string& s)
{
    if (s == "EQ")  return InternalFxId::kEq;
    if (s == "CMP") return InternalFxId::kCmp;
    if (s == "RVB") return InternalFxId::kRvb;
    if (s == "DLY") return InternalFxId::kDly;
    throw std::invalid_argument ("ida::internalFxIdFromString: unknown id \"" + s + "\"");
}

} // namespace ida
```

- [ ] **Step 5: Run test to verify it passes**

Run:
```bash
cmake --build build --target IdaTests && ctest --test-dir build -R InternalFxId --output-on-failure
```
Expected: PASS — all four test cases green.

- [ ] **Step 6: Run the full test suite to confirm no regression**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: 569 pass + the 1 documented Not-Run (`MainComponentPluginEditorTests_NOT_BUILT`). No new failures.

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/InternalFxId.h tests/InternalFxIdTests.cpp tests/CMakeLists.txt
git commit -m "feat: P7 T2 step 1 — InternalFxId header + wire-stable to/from-string round-trip"
```

---

### Task 2: Widen EffectChainEntry to a tagged union

**Files:**
- Modify: `core/include/ida/EffectChain.h` (add `EffectChainSlotKind` enum, two new fields + two factory statics, update `operator==`)
- Modify: `core/src/EffectChain.cpp` (out-of-line factory impls)
- Modify: `tests/EffectChainTests.cpp` (migrate `makeEntry` helper to `makePlugin`; add new tests for Internal-kind construction + COW preservation across kinds)

- [ ] **Step 1: Write the failing tests**

Edit `tests/EffectChainTests.cpp`. First migrate the existing `makeEntry` helper (lines 29-42) to use the new factory:

```cpp
// Existing fixtures used to assemble plugin-kind entries by hand. After the
// union widening this is a one-call factory; keeping it as a helper preserves
// the spelling of every downstream test case.
EffectChainEntry makeEntry (const char* uniqueId, const char* name)
{
    ida::PluginDescriptor desc;
    desc.format       = ida::PluginFormat::Vst3;
    desc.uniqueId     = uniqueId;
    desc.version      = "1.0.0";
    desc.name         = name;
    desc.manufacturer = "AcmeAudio";
    desc.filePath     = std::string ("/plugins/") + name + ".vst3";
    return EffectChainEntry::makePlugin (std::move (desc), name, "abc=");
}
```

Then add new test cases at the bottom of the file:

```cpp
TEST_CASE ("EffectChainEntry default-constructs as Empty kind", "[effect-chain][union-slot]")
{
    // A fresh entry must NOT silently look like a Plugin — empty-by-default
    // protects callers who forget to set the discriminant.
    EffectChainEntry e;
    CHECK (e.kind == ida::EffectChainSlotKind::Empty);
    CHECK (e.descriptor.uniqueId.empty());
}

TEST_CASE ("EffectChainEntry::makeInternal stamps the kind + internalId", "[effect-chain][union-slot]")
{
    const auto eq  = EffectChainEntry::makeInternal (ida::InternalFxId::kEq);
    const auto cmp = EffectChainEntry::makeInternal (ida::InternalFxId::kCmp);

    CHECK (eq.kind == ida::EffectChainSlotKind::Internal);
    CHECK (eq.internalId == ida::InternalFxId::kEq);

    CHECK (cmp.kind == ida::EffectChainSlotKind::Internal);
    CHECK (cmp.internalId == ida::InternalFxId::kCmp);

    // descriptor stays default-empty on an Internal slot (no plugin payload)
    CHECK (eq.descriptor.uniqueId.empty());
}

TEST_CASE ("EffectChainEntry::makePlugin stamps the kind + descriptor", "[effect-chain][union-slot]")
{
    ida::PluginDescriptor d;
    d.format       = ida::PluginFormat::Vst3;
    d.uniqueId     = "EQ-1";
    d.name         = "Saturn EQ";
    d.manufacturer = "AcmeAudio";

    const auto e = EffectChainEntry::makePlugin (d, "EQ", "");

    CHECK (e.kind == ida::EffectChainSlotKind::Plugin);
    CHECK (e.descriptor.uniqueId == "EQ-1");
    CHECK (e.displayName == "EQ");
}

TEST_CASE ("EffectChainEntry equality includes the kind + internalId", "[effect-chain][union-slot]")
{
    const auto a = EffectChainEntry::makeInternal (ida::InternalFxId::kEq);
    const auto b = EffectChainEntry::makeInternal (ida::InternalFxId::kEq);
    const auto c = EffectChainEntry::makeInternal (ida::InternalFxId::kCmp);

    EffectChainEntry empty;          // kind == Empty
    EffectChainEntry pluginLooking;  // kind == Empty but with plugin data filled in
    pluginLooking.descriptor.uniqueId = "EQ-1";

    CHECK (a == b);
    CHECK (a != c);
    CHECK (a != empty);
    CHECK (empty != pluginLooking);  // descriptor mismatch is still detected on Empty entries
}

TEST_CASE ("a chain can mix Internal and Plugin slots", "[effect-chain][union-slot]")
{
    ida::PluginDescriptor d;
    d.format = ida::PluginFormat::Vst3;
    d.uniqueId = "RV-1";
    d.name = "Plate Reverb";

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (ida::InternalFxId::kEq))
                 .withAppended (EffectChainEntry::makePlugin (d, "Reverb", ""))
                 .withAppended (EffectChainEntry::makeInternal (ida::InternalFxId::kCmp));

    REQUIRE (chain.size() == 3);
    CHECK (chain.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (chain.entries()[0].internalId == ida::InternalFxId::kEq);
    CHECK (chain.entries()[1].kind == ida::EffectChainSlotKind::Plugin);
    CHECK (chain.entries()[1].descriptor.uniqueId == "RV-1");
    CHECK (chain.entries()[2].kind == ida::EffectChainSlotKind::Internal);
    CHECK (chain.entries()[2].internalId == ida::InternalFxId::kCmp);
}
```

- [ ] **Step 2: Run tests to verify they fail (factories don't exist)**

Run:
```bash
cmake --build build --target IdaTests 2>&1 | head -40
```
Expected: compile FAIL on `EffectChainEntry::makePlugin` / `makeInternal` not found, and on `EffectChainSlotKind` undefined.

- [ ] **Step 3: Widen the header**

Edit `core/include/ida/EffectChain.h`. Add the includes + new enum + factory declarations + the two new fields. The full updated struct:

```cpp
#pragma once

#include "ida/ArchivalMode.h"
#include "ida/InternalFxId.h"
#include "ida/PluginDescriptor.h"
#include "ida/VersionPinningRecord.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ida
{

/// Discriminant for `EffectChainEntry`'s tagged union (the "union slot type"
/// contract in `docs/design/ida-internal-fx.md`). Each slot is one of:
///   - `Empty`    — slot is unallocated; host skips it at render time.
///   - `Internal` — slot identifies one of IDA's four built-in FX by
///                  `InternalFxId`; the host wraps the matching OTTO Player FX.
///   - `Plugin`   — slot carries a `PluginDescriptor` for an externally
///                  hosted VST/CLAP/AU(v3). Unchanged from the pre-union shape.
/// Wire-stable order: do not reorder values without a SessionFormat migration.
enum class EffectChainSlotKind : std::uint8_t
{
    Empty    = 0,
    Internal = 1,
    Plugin   = 2,
};

/// One slot in an effect chain — a tagged union over Empty / Internal / Plugin.
/// `kind` is the discriminant; `internalId` is meaningful iff `kind == Internal`;
/// `descriptor` + `stateBase64` + `archivalMode` + `persistedSnapshot` are
/// meaningful iff `kind == Plugin`. Default construction yields an Empty slot
/// — a safer default than Plugin, because an uninitialized entry with
/// `kind == Plugin` and a default `descriptor` would look like a legitimate
/// (but invalid) plugin reference. Use the static factories `makePlugin` and
/// `makeInternal` at construction sites; the field-by-field constructor is
/// retained only because it is implicit in the aggregate-initialization style
/// the persistence layer uses.
///
/// The state blob is an opaque byte string (base64-encoded for JSON safety)
/// produced by the host runtime when it serializes a plugin; the data model
/// does not interpret it.
///
/// `archivalMode` selects the per-instance strategy for handling plug-in
/// non-determinism (white paper §15.6). Default is `VersionPinning` per V7
/// plan line 563. `persistedSnapshot` is the frozen identity at the moment
/// of the last `serializeSession`; populated by `populateVersionPinningRecords`
/// on save and compared by `verifyVersionPinningOnLoad` on load. Both fields
/// are meaningless on `Empty` and `Internal` slots and are persisted only on
/// `Plugin` slots.
struct EffectChainEntry
{
    EffectChainSlotKind kind { EffectChainSlotKind::Empty };
    InternalFxId        internalId { InternalFxId::kEq }; ///< valid iff kind == Internal

    PluginDescriptor descriptor;                          ///< valid iff kind == Plugin
    std::string      displayName;                         ///< chain-local name
    std::string      stateBase64;                         ///< plugin state as base64 (Plugin only)
    bool             bypassed { false };
    ArchivalMode     archivalMode { ArchivalMode::VersionPinning }; ///< Plugin only
    std::optional<VersionPinningRecord> persistedSnapshot;          ///< Plugin only

    /// Factory: construct an Internal slot for a built-in FX.
    static EffectChainEntry makeInternal (InternalFxId id);

    /// Factory: construct a Plugin slot from a descriptor + display name +
    /// state blob (state may be empty when not yet captured).
    static EffectChainEntry makePlugin (PluginDescriptor descriptor,
                                        std::string      displayName,
                                        std::string      stateBase64 = "");

    bool operator== (const EffectChainEntry& other) const noexcept
    {
        return kind == other.kind
            && internalId == other.internalId
            && descriptor == other.descriptor
            && displayName == other.displayName
            && stateBase64 == other.stateBase64
            && bypassed == other.bypassed
            && archivalMode == other.archivalMode
            && persistedSnapshot == other.persistedSnapshot;
    }
    bool operator!= (const EffectChainEntry& other) const noexcept { return ! (*this == other); }
};

// (the rest of the file — class EffectChain — stays exactly as before)
```

The `EffectChain` class itself does not change in this task. Leave lines 46-96 (the class declaration, kMaxSlots, withAppended/withReplaced/withRemoved/withMoved) verbatim.

- [ ] **Step 4: Implement the factories**

Edit `core/src/EffectChain.cpp`. Append the two factory implementations at the bottom of the file, inside the `namespace ida` block, after `EffectChain::withMoved`:

```cpp
EffectChainEntry EffectChainEntry::makeInternal (InternalFxId id)
{
    EffectChainEntry e;
    e.kind = EffectChainSlotKind::Internal;
    e.internalId = id;
    e.displayName = internalFxIdToString (id);
    return e;
}

EffectChainEntry EffectChainEntry::makePlugin (PluginDescriptor descriptor,
                                               std::string      displayName,
                                               std::string      stateBase64)
{
    EffectChainEntry e;
    e.kind = EffectChainSlotKind::Plugin;
    e.descriptor = std::move (descriptor);
    e.displayName = std::move (displayName);
    e.stateBase64 = std::move (stateBase64);
    return e;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run:
```bash
cmake --build build --target IdaTests && ctest --test-dir build -R "effect-chain|effectchain|union-slot" --output-on-failure
```
Expected: PASS — both the migrated existing fixtures and the new union-slot tests are green. The previously green `[effect-chain][cap]` cases stay green (cap behaviour unchanged).

- [ ] **Step 6: Run the full test suite to confirm no regression**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: 569 pass + the 1 documented Not-Run. No new failures. In particular, persistence tests (`[sessionformat]`) MUST stay green — the new fields default to `Empty` / `kEq` and the existing serializer ignores them in this task (Task 3 wires them).

If `[sessionformat]` tests fail because the old serializer reads back entries as `Empty`-kind (since persistence does not yet set `kind`), this is a real coupling — the existing round-trip test in `EffectChainTests.cpp` does `CHECK (*round->effectChain() == chain)`, and if the in-code chain is Plugin-kind but the round-tripped chain is Empty-kind, equality fails. The migrated `makeEntry` helper produces Plugin-kind entries, and Task 3 has not yet wired the kind through. If this fails, the fix is to either:

- (a) Land Task 3 immediately to make the round-trip preserve the kind, or
- (b) In Task 2 only, in `effectChainEntryFromVar`, default missing `kind` to `Plugin` so the existing fixtures stay equal across the round-trip even before Task 3's full discriminant emission.

Pick (b) for Task 2 to keep this task self-contained. Edit `persistence/src/SessionFormat.cpp:562-585` and add a one-line backstop after the existing default-init of `EffectChainEntry e`:

```cpp
e.kind = EffectChainSlotKind::Plugin; // Task 2 backstop: old JSON had no `kind` field;
                                      // every entry it could encode was a plugin.
                                      // Task 3 replaces this with proper discriminant read.
```

If you take backstop (b), include a one-line comment naming it as the Task 2 backstop so Task 3 knows to remove it.

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/EffectChain.h core/src/EffectChain.cpp tests/EffectChainTests.cpp persistence/src/SessionFormat.cpp
git commit -m "feat: P7 T2 step 2 — EffectChainEntry as tagged union (Empty/Internal/Plugin) + factories"
```

---

### Task 3: Wire the discriminant through SessionFormat

**Files:**
- Modify: `persistence/src/SessionFormat.cpp` (extend `effectChainEntryToVar` lines 549-560 + `effectChainEntryFromVar` lines 562-585; remove the Task 2 backstop comment if present)
- Modify: `tests/EffectChainTests.cpp` (add Internal-kind round-trip tests + a pre-union back-compat test)

- [ ] **Step 1: Write the failing tests**

Append to `tests/EffectChainTests.cpp`:

```cpp
TEST_CASE ("an Internal effect entry round-trips through SessionFormat",
           "[effect-chain][sessionformat][union-slot]")
{
    using ida::EffectChainEntry;
    using ida::InternalFxId;

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq))
                 .withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp));

    const Constituent c =
        Constituent (ConstituentId (11), Position(), Position (Rational (4)))
            .withName ("internal-only")
            .withEffectChain (chain);

    const auto json  = ida::persistence::serializeSession (c);
    const auto round = ida::persistence::deserializeSession (json);

    REQUIRE (round->hasEffectChain());
    REQUIRE (round->effectChain()->size() == 2);
    CHECK (round->effectChain()->entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round->effectChain()->entries()[0].internalId == InternalFxId::kEq);
    CHECK (round->effectChain()->entries()[1].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round->effectChain()->entries()[1].internalId == InternalFxId::kCmp);
    CHECK (*round->effectChain() == chain);
}

TEST_CASE ("a mixed Internal + Plugin chain round-trips through SessionFormat",
           "[effect-chain][sessionformat][union-slot]")
{
    using ida::EffectChainEntry;
    using ida::InternalFxId;

    ida::PluginDescriptor d;
    d.format = ida::PluginFormat::Vst3;
    d.uniqueId = "RV-1";
    d.version = "1.0.0";
    d.name = "Plate Reverb";
    d.manufacturer = "AcmeAudio";
    d.filePath = "/plugins/Plate Reverb.vst3";

    EffectChain chain;
    chain = chain.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq))
                 .withAppended (EffectChainEntry::makePlugin (d, "Reverb", "ZmFrZS1zdGF0ZQ=="))
                 .withAppended (EffectChainEntry::makeInternal (InternalFxId::kDly));

    const Constituent c =
        Constituent (ConstituentId (12), Position(), Position (Rational (4)))
            .withName ("mixed")
            .withEffectChain (chain);

    const auto json  = ida::persistence::serializeSession (c);
    const auto round = ida::persistence::deserializeSession (json);

    REQUIRE (round->hasEffectChain());
    REQUIRE (round->effectChain()->size() == 3);
    CHECK (*round->effectChain() == chain);
}

TEST_CASE ("a pre-union session (no `kind` field) loads every entry as Plugin",
           "[effect-chain][sessionformat][union-slot][forward-compat]")
{
    // Forward-compat: pre-union JSON had no `kind` field. Every entry it could
    // encode was a hosted plugin (Internal didn't exist yet). The deserializer
    // must default missing `kind` to Plugin so old sessions load.
    //
    // Rather than hand-write a fixture (brittle against envelope changes), we
    // round-trip a real Plugin-kind chain, strip the `kind` field from each
    // entry in the parsed JSON tree, re-serialize, and feed THAT to the
    // deserializer. This proves the missing-kind-default path independently
    // of the current envelope shape.
    EffectChain chain;
    chain = chain.withAppended (makeEntry ("legacy-eq", "Legacy EQ"));

    const Constituent c =
        Constituent (ConstituentId (100), Position(), Position (Rational (4)))
            .withName ("legacy")
            .withEffectChain (chain);

    const auto jsonWithKind = ida::persistence::serializeSession (c);

    // Parse, walk effects.entries, drop the "kind" property from each entry,
    // re-emit. We do not assume envelope structure beyond `effects.entries`
    // (the only path the union touches).
    juce::var parsed;
    REQUIRE (juce::JSON::parse (jsonWithKind, parsed).wasOk());

    auto stripKindInEffects = [] (auto& self, juce::var& node) -> void {
        if (auto* obj = node.getDynamicObject())
        {
            if (obj->hasProperty ("entries"))
            {
                auto entries = obj->getProperty ("entries");
                if (entries.isArray())
                {
                    for (int i = 0; i < entries.size(); ++i)
                    {
                        if (auto* entry = entries[i].getDynamicObject())
                            entry->removeProperty ("kind");
                    }
                }
            }
            for (auto& prop : obj->getProperties())
                self (self, prop.value);
        }
        else if (node.isArray())
        {
            for (int i = 0; i < node.size(); ++i)
                self (self, node.getReference (i));
        }
    };
    stripKindInEffects (stripKindInEffects, parsed);

    const auto preUnionJson = juce::JSON::toString (parsed);

    const auto round = ida::persistence::deserializeSession (preUnionJson);
    REQUIRE (round->hasEffectChain());
    REQUIRE (round->effectChain()->size() == 1);
    const auto& entry = round->effectChain()->entries()[0];
    CHECK (entry.kind == ida::EffectChainSlotKind::Plugin);
    CHECK (entry.descriptor.uniqueId == "legacy-eq");
}
```

Add `#include <juce_core/juce_core.h>` at the top of `tests/EffectChainTests.cpp` if it is not already pulled in transitively (`SessionFormat.h` already includes `juce::String` types, so it likely is — confirm by compiling; if it errors on `juce::var`, add the include).

- [ ] **Step 2: Run tests to verify they fail (discriminant not yet wired)**

Run:
```bash
cmake --build build --target IdaTests && ctest --test-dir build -R "union-slot" --output-on-failure
```
Expected: FAIL on the Internal-only and mixed round-trips — the serializer currently emits a `plugin` block for every entry (because `effectChainEntryToVar` ignores `kind`), so the round-tripped Internal entry comes back as `Plugin` with a default descriptor, and equality fails. The forward-compat test should pass (Task 2 backstop already defaults missing `kind` to `Plugin`).

- [ ] **Step 3: Extend `effectChainEntryToVar`**

Edit `persistence/src/SessionFormat.cpp` at lines 549-560. Replace the function body with a discriminant-aware emitter:

```cpp
juce::var effectChainEntryToVar (const EffectChainEntry& e)
{
    auto obj = makeObject();

    // Discriminant first — readers consult this before deciding which payload
    // fields to look for. Wire string is the canonical name of the enum.
    obj->setProperty ("kind",
        juce::String (e.kind == EffectChainSlotKind::Empty    ? "Empty"
                    : e.kind == EffectChainSlotKind::Internal ? "Internal"
                    :                                            "Plugin"));

    obj->setProperty ("displayName", juce::String (e.displayName));
    obj->setProperty ("bypassed",    e.bypassed);

    switch (e.kind)
    {
        case EffectChainSlotKind::Empty:
            // No payload beyond kind + displayName + bypassed.
            break;

        case EffectChainSlotKind::Internal:
            obj->setProperty ("internalId", juce::String (internalFxIdToString (e.internalId)));
            // archivalMode + state + persistedSnapshot are Plugin-only.
            break;

        case EffectChainSlotKind::Plugin:
            obj->setProperty ("plugin",       pluginDescriptorToVar (e.descriptor));
            obj->setProperty ("state",        juce::String (e.stateBase64));
            obj->setProperty ("archivalMode", juce::String (archivalModeToString (e.archivalMode)));
            if (e.persistedSnapshot.has_value())
                obj->setProperty ("persistedSnapshot", versionPinningRecordToVar (*e.persistedSnapshot));
            break;
    }

    return objectVar (obj);
}
```

- [ ] **Step 4: Extend `effectChainEntryFromVar`**

Same file, lines 562-585. Replace the body with a discriminant-aware reader (and remove the Task 2 backstop comment if it was added):

```cpp
EffectChainEntry effectChainEntryFromVar (const juce::var& v)
{
    EffectChainEntry e;

    // Forward-compat: pre-union JSON had no `kind` field. Every entry it
    // could encode was a plugin (Internal did not exist). Default missing
    // `kind` to Plugin so old sessions load without migration.
    EffectChainSlotKind kind = EffectChainSlotKind::Plugin;
    if (auto* obj = v.getDynamicObject(); obj != nullptr && obj->hasProperty ("kind"))
    {
        const auto s = obj->getProperty ("kind").toString().toStdString();
        if      (s == "Empty")    kind = EffectChainSlotKind::Empty;
        else if (s == "Internal") kind = EffectChainSlotKind::Internal;
        else if (s == "Plugin")   kind = EffectChainSlotKind::Plugin;
        else fail ("effectChainEntry.kind unknown value \"" + s + "\"");
    }
    e.kind = kind;

    // Common fields. `displayName` is required on every kind; older sessions
    // already encode it. `bypassed` defaults to false when absent.
    if (auto* obj = v.getDynamicObject(); obj != nullptr)
    {
        if (obj->hasProperty ("displayName"))
            e.displayName = obj->getProperty ("displayName").toString().toStdString();
        if (obj->hasProperty ("bypassed"))
            e.bypassed = bool (obj->getProperty ("bypassed"));
    }

    switch (kind)
    {
        case EffectChainSlotKind::Empty:
            break; // no further payload

        case EffectChainSlotKind::Internal:
        {
            if (auto* obj = v.getDynamicObject(); obj != nullptr && obj->hasProperty ("internalId"))
                e.internalId = internalFxIdFromString (
                    obj->getProperty ("internalId").toString().toStdString());
            else
                fail ("effectChainEntry of kind Internal missing required `internalId`");
            break;
        }

        case EffectChainSlotKind::Plugin:
        {
            e.descriptor  = pluginDescriptorFromVar (requireProperty (v, "plugin"));
            e.stateBase64 = requireProperty (v, "state").toString().toStdString();
            // archivalMode + persistedSnapshot are M8 additions. Sessions
            // serialized before M8 do not carry them — default archivalMode
            // to VersionPinning and leave the optional snapshot empty.
            if (auto* obj = v.getDynamicObject(); obj != nullptr)
            {
                if (obj->hasProperty ("archivalMode"))
                    e.archivalMode = archivalModeFromString (
                        obj->getProperty ("archivalMode").toString());
                if (obj->hasProperty ("persistedSnapshot"))
                    e.persistedSnapshot = versionPinningRecordFromVar (
                        obj->getProperty ("persistedSnapshot"));
            }
            break;
        }
    }

    return e;
}
```

Notice two changes vs the original:

- `displayName` and `bypassed` are now read defensively (`hasProperty`) rather than via `requireProperty`, because `Empty` kind entries do not need them. The existing tests all set both, so behaviour is identical for the legacy path.
- The `Plugin` branch keeps `requireProperty` on the plugin descriptor and on `state`, preserving the original strict-read contract for plugin entries.

- [ ] **Step 5: Run tests to verify they pass**

Run:
```bash
cmake --build build --target IdaTests && ctest --test-dir build -R "union-slot|effect-chain|effectchain|sessionformat" --output-on-failure
```
Expected: PASS — all three new round-trip tests + every previously-green effect-chain and sessionformat test.

- [ ] **Step 6: Run the full test suite to confirm no regression**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: 569 pass + the 1 documented Not-Run. No new failures.

- [ ] **Step 7: Commit**

```bash
git add persistence/src/SessionFormat.cpp tests/EffectChainTests.cpp
git commit -m "feat: P7 T2 step 3 — SessionFormat round-trips the EffectChainEntry kind discriminant + forward-compat"
```

---

### Task 4: Mixer-graph integration round-trips

**Files:**
- Modify: `tests/MixerGraphPersistenceTests.cpp` (add Internal-FX insert round-trip cases for InputMixer and OutputMixer)

- [ ] **Step 1: Write the failing test**

Append to `tests/MixerGraphPersistenceTests.cpp`:

```cpp
TEST_CASE ("InputMixer survives serialize -> deserialize with an Internal-FX insert chain",
           "[sessionformat][mixer][union-slot]")
{
    using ida::EffectChainEntry;
    using ida::InternalFxId;

    ida::engine::InputMixer source;
    // Seed a bus, give it an Internal-EQ insert; seed a channel, give it an
    // Internal-CMP insert. Both must survive the round-trip with kind +
    // internalId preserved.
    const auto bus = source.addFxReturn ("drums");
    source.setBusEffectChain (bus,
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kEq)));

    auto strip = source.addChannel ("kick");
    strip->setEffectChain (
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kCmp)));

    const auto state = source.exportGraphState();
    const auto json  = ida::persistence::serializeMixerGraphState (state);
    const auto round = ida::persistence::deserializeInputMixerGraphState (json);

    REQUIRE (round.buses.size() == 1);
    REQUIRE (round.buses[0].inserts.size() == 1);
    CHECK (round.buses[0].inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round.buses[0].inserts.entries()[0].internalId == InternalFxId::kEq);

    REQUIRE (round.channels.size() == 1);
    REQUIRE (round.channels[0].inserts.size() == 1);
    CHECK (round.channels[0].inserts.entries()[0].kind == ida::EffectChainSlotKind::Internal);
    CHECK (round.channels[0].inserts.entries()[0].internalId == InternalFxId::kCmp);
}

TEST_CASE ("OutputMixer survives serialize -> deserialize with an Internal-FX insert chain",
           "[sessionformat][mixer][union-slot]")
{
    using ida::EffectChainEntry;
    using ida::InternalFxId;

    ida::engine::OutputMixer source;
    const auto aux = source.addFxReturn ("aux");
    source.setBusEffectChain (aux,
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kRvb)));

    auto strip = source.addChannel ("phrase-1");
    strip->setEffectChain (
        ida::EffectChain{}.withAppended (EffectChainEntry::makeInternal (InternalFxId::kDly)));

    const auto state = source.exportGraphState();
    const auto json  = ida::persistence::serializeMixerGraphState (state);
    const auto round = ida::persistence::deserializeOutputMixerGraphState (json);

    REQUIRE (round.buses.size() == 1);
    REQUIRE (round.buses[0].inserts.entries()[0].internalId == InternalFxId::kRvb);
    REQUIRE (round.channels.size() == 1);
    REQUIRE (round.channels[0].inserts.entries()[0].internalId == InternalFxId::kDly);
}
```

The exact names of `addFxReturn` / `addChannel` / `setBusEffectChain` / `setEffectChain` / `exportGraphState` come from the existing test cases at lines 24-80 of the same file — mirror those calls verbatim. If the existing API differs (e.g. `addFxReturn` returns a `BusId` vs an iterator), read the existing test fixture once, then translate. Do not invent new mixer API.

- [ ] **Step 2: Run tests to verify they pass on the first attempt**

Run:
```bash
cmake --build build --target IdaTests && ctest --test-dir build -R "mixer.*union-slot" --output-on-failure
```
Expected: PASS — Tasks 1-3 already wired the discriminant end-to-end through `effectChainToVar` (which `inputChannelToVar` / `outputChannelToVar` / bus serialization at lines 857/887/913 already use). This task is *verification* that the contract holds at the mixer-graph integration layer; if it fails, that proves Task 3 was incomplete (e.g. missed one of the three serializer call sites). Run `grep -n effectChainToVar persistence/src/SessionFormat.cpp` to confirm all four call sites are reached by the existing serializers.

- [ ] **Step 3: Run the full test suite to confirm no regression**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: 569 pass + the 1 documented Not-Run. No new failures. The MixerGraphPersistenceTests file now carries two additional `[union-slot]`-tagged cases.

- [ ] **Step 4: Commit**

```bash
git add tests/MixerGraphPersistenceTests.cpp
git commit -m "test: P7 T2 step 4 — Internal-FX inserts round-trip through Input/OutputMixer graph JSON"
```

---

## Self-review checklist (to be run by the implementer before declaring T2 done)

- [ ] `grep -n 'EffectChainSlotKind::Plugin' core/include/ida/EffectChain.h` shows the new enum is referenced.
- [ ] `grep -rn 'makeEntry\|makePlugin\|makeInternal' tests/` shows every test fixture that builds a Plugin-kind entry uses the factory.
- [ ] `grep -n 'kind' persistence/src/SessionFormat.cpp` shows the discriminant is emitted and read.
- [ ] `ctest --test-dir build --output-on-failure` reports 569 pass + 1 Not-Run, same as baseline (no new failures, no skipped tests).
- [ ] No UI files touched: `git diff --name-only HEAD~4..HEAD -- app/ ui/` is empty (4 = the four T2 commits).
- [ ] No OTTO submodule SHA change: `git status` in `external/OTTO/` is clean.
- [ ] No `engine/src/fx/` directory created (that is T3 territory).
- [ ] All four commits push cleanly to `origin/master` (subagents push their own commits per `feedback_subagents_push_to_master`).

## What this plan does NOT do

These belong to T3 and later:

- Adapter classes wrapping OTTO's Player FX (`engine/src/fx/Eq.cpp` etc.).
- Runtime instantiation of `Internal` slots — `EffectChain::resolvedEffectFor(...)` and `InternalFxFactory.h`.
- Any RT-safe parameter swap pattern (config-swap, UI scratch → release-store).
- Any UI surface (insert picker, Sends tab, detail-tab switcher).
- Wiring P4/P5 mixer-graph persistence into `MainComponent` save / load (that is T6).
- Plugin scanner repair (own slice "P7-scanner").

If a step in this plan tempts you toward any of the above, stop and re-read the umbrella plan section T2 — that is the scope boundary.
