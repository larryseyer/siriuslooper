# Mixer Routing Graph — Phase 4: Per-Node Insert Chains — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every mixer node an insert chain — channels gain an `EffectChain` + `IEffectChainHost` dispatch exactly as `Bus` already has, and `EffectChain` gains an 8-slot cap that therefore binds channels, buses, and FX returns alike.

**Architecture:** `EffectChain` (core, the shared structural type for all three node kinds) grows a hard 8-slot cap on `withAppended`. `ChannelStrip<Audio>` (engine, the per-channel node owned by both mixers) grows the same set-once `setEffectChain` / `setEffectChainHost` collaborators `Bus` already has, and its `process` dispatches each non-bypassed slot through `host_->pumpSlot(...)` **pre-fader** (before gain/pan/width), in-place on the caller's buffer — so the existing post-fader meter still reflects the channel's contribution. The dispatch is byte-for-byte inert when no host is bound or every slot is empty/bypassed, exactly mirroring `Bus`'s two-path structure. The existing out-of-process host is reused unchanged: its `pumpSlot`/`configureBus` are keyed by an opaque `std::int64_t`, so a channel is just another key — no host changes are needed for this apparatus.

**Tech Stack:** C++17, JUCE-free engine/core public headers, Catch2 unit tests, CMake/Ninja, the existing M7 `OutOfProcessEffectChainHost` (`pumpSlot`'s 1-buffer-delay model).

---

## Scope & boundaries (read before starting)

This phase ships the **engine + core apparatus, tested with a synchronous mock `IEffectChainHost`** — the same posture Phases 1–3 took (apparatus proven headless; production wiring lands in the UI phases). Concretely:

- **In scope:** the 8-slot cap on `EffectChain`; the `EffectChain` + host collaborators on `ChannelStrip<Audio>`; the pre-fader insert dispatch in `ChannelStrip<Audio>::process`; full TDD coverage (dispatch, ordering, bypass, behavior-equivalence, mute interaction, pumpSlot-miss, RT-safety static-assert).
- **Out of scope (deferred, record in `todo.md`):**
  - **Production wiring.** No mixer calls `setEffectChainHost` outside tests; `InputMixer`/`OutputMixer`/`MainComponent` are **untouched**. Channels default to no host = the pre-Phase-4 path, byte-identical. Wiring (mixer owns/holds an `OutOfProcessEffectChainHost`, allocates a non-colliding `int64` key space for channel-vs-bus chains, calls `configureBus` per channel) lands with the Input/Output Mixer insert-management UI (Phases 6–7).
  - **Real VST/CLAP loading.** Satisfied by the unchanged `int64`-keyed `OutOfProcessEffectChainHost`; this phase proves dispatch with a mock host (the `HalvingEffectHost` pattern already in `OutputMixerTests.cpp`).
  - **Built-in IDA FX as slot contents** — the explicit follow-on spec; this phase ships no selectable-but-dead effects.
  - **Renaming `OutOfProcessEffectChainHost::configureBus` → `configureNode`.** The name is historical; the API is already node-agnostic (`std::int64_t` key). A rename touches the host + all call sites + tests for zero behavior change — out of scope, surgical-changes rule. Note as a future tidy in `todo.md`.

**Key-space note (for the future wiring phase, not this phase):** `pumpSlot`/`configureBus` key on a single `std::int64_t`. When channels and buses are eventually hosted on the **same** `OutOfProcessEffectChainHost` instance, `ChannelId` and `BusId` values can collide (both start at small integers). The wiring phase must either give each mixer its own host instance (cleanest) or partition the `int64` space (e.g. channels in the high half). `ChannelStrip` takes the key as an explicit `setEffectChainHost(host, nodeKey)` argument precisely so the **caller** owns that decision. This phase's tests pass distinct keys and assert the key is forwarded verbatim.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `core/include/ida/EffectChain.h` | Shared structural chain type | Add `kMaxSlots`, `full()`, doc the `withAppended` cap |
| `core/src/EffectChain.cpp` | `withAppended`/`withReplaced`/… bodies | Guard `withAppended` at the cap |
| `tests/EffectChainTests.cpp` | Chain unit tests | Add `[effect-chain][cap]` cases |
| `engine/include/ida/ChannelStrip.h` | Per-channel audio node (header-only template) | Add `EffectChain` + host collaborators + pre-fader insert dispatch in `process` |
| `tests/ChannelStripTests.cpp` | ChannelStrip unit tests | Add `[channel-strip][inserts]` cases + mock hosts |

No CMake changes (both test files are already registered in `tests/CMakeLists.txt`). No host changes. No mixer changes.

---

### Task 1: `EffectChain` 8-slot cap (core)

The cap lives on the shared type, so it binds channels, buses, and FX returns simultaneously — that is the spec's "the cap applies to buses and returns too." `withReplaced` / `withRemoved` / `withMoved` never grow the chain, so only `withAppended` needs guarding. Throwing `std::length_error` is consistent with the existing `withReplaced`/`withRemoved`/`withMoved` throwing `std::out_of_range`; a `full()` predicate lets UI callers check before offering "add a slot" rather than using the exception for control flow.

**Files:**
- Modify: `core/include/ida/EffectChain.h`
- Modify: `core/src/EffectChain.cpp`
- Test: `tests/EffectChainTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/EffectChainTests.cpp` (the `makeEntry` helper at the top of the file is already in scope; `#include <stdexcept>` is already present):

```cpp
TEST_CASE ("EffectChain caps at kMaxSlots (8) appended slots", "[effect-chain][cap]")
{
    EffectChain chain;
    REQUIRE (EffectChain::kMaxSlots == 8u);

    for (std::size_t i = 0; i < EffectChain::kMaxSlots; ++i)
    {
        CHECK_FALSE (chain.full());
        chain = chain.withAppended (makeEntry ("fx", "Fx"));
    }

    CHECK (chain.size() == EffectChain::kMaxSlots);
    CHECK (chain.full());
    CHECK_THROWS_AS (chain.withAppended (makeEntry ("over", "Over")), std::length_error);
}

TEST_CASE ("EffectChain at the cap still allows replace / remove / move", "[effect-chain][cap]")
{
    EffectChain chain;
    for (std::size_t i = 0; i < EffectChain::kMaxSlots; ++i)
        chain = chain.withAppended (makeEntry ("fx", "Fx"));
    REQUIRE (chain.full());

    // Non-growing edits are unaffected by the cap.
    CHECK_NOTHROW (chain.withReplaced (0, makeEntry ("repl", "Repl")));
    CHECK_NOTHROW (chain.withMoved (0, 7));

    // Removing drops below the cap and re-enables append.
    const EffectChain shortened = chain.withRemoved (0);
    CHECK_FALSE (shortened.full());
    CHECK_NOTHROW (shortened.withAppended (makeEntry ("again", "Again")));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[effect-chain][cap]"`
Expected: compile error — `kMaxSlots` and `full()` are not members of `EffectChain` yet.

- [ ] **Step 3: Add `kMaxSlots` + `full()` to the header**

In `core/include/ida/EffectChain.h`, inside `class EffectChain`'s `public:` section, immediately after the `EffectChain() = default;` line, add:

```cpp
    /// Hard per-node insert-slot ceiling (routing-graph Phase 4). The cap
    /// lives on this shared type, so it binds every node that owns a chain —
    /// channels, buses, and FX returns alike. `withAppended` throws
    /// std::length_error when the chain is already at this size.
    static constexpr std::size_t kMaxSlots = 8;

    /// True when the chain holds `kMaxSlots` entries — no further append is
    /// possible. UI callers check this before offering "add a slot" so the
    /// cap is not enforced via exception-as-control-flow.
    bool full() const noexcept { return entries_.size() >= kMaxSlots; }
```

- [ ] **Step 4: Guard `withAppended` at the cap**

In `core/src/EffectChain.cpp`, replace the body of `EffectChain::withAppended`:

```cpp
EffectChain EffectChain::withAppended (EffectChainEntry entry) const
{
    if (entries_.size() >= kMaxSlots)
        throw std::length_error ("EffectChain: cannot append past kMaxSlots (8)");

    EffectChain next (*this);
    next.entries_.push_back (std::move (entry));
    return next;
}
```

Ensure `#include <stdexcept>` is present at the top of `core/src/EffectChain.cpp` (the existing `withReplaced`/`withRemoved`/`withMoved` already throw `std::out_of_range`, so it almost certainly is — add it if missing).

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[effect-chain][cap]"`
Expected: PASS, 2 cases.

- [ ] **Step 6: Run the full EffectChain tag to confirm no regression**

Run: `./build/tests/IdaTests "[effect-chain]"`
Expected: PASS (existing cases + the 2 new ones).

- [ ] **Step 7: Commit**

```bash
git add core/include/ida/EffectChain.h core/src/EffectChain.cpp tests/EffectChainTests.cpp
git commit -m "feat: EffectChain 8-slot cap (binds channels/buses/returns)"
```

---

### Task 2: `ChannelStrip<Audio>` gains chain + host collaborators (engine)

Mirror `Bus`'s set-once collaborators. `Bus` derives its `pumpSlot` key from its own `id_`; `ChannelStrip<Audio>` has no id field, so `setEffectChainHost` takes the key explicitly — the caller owns key-space allocation (see the key-space note above). No dispatch yet; this task only adds the accessors so `process` (Task 3) has something to read.

**Files:**
- Modify: `engine/include/ida/ChannelStrip.h`
- Test: `tests/ChannelStripTests.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/ChannelStripTests.cpp`. First add these includes near the existing includes at the top of the file (after `#include "sirius/SignalType.h"`):

```cpp
#include "sirius/EffectChain.h"
#include "sirius/IEffectChainHost.h"
#include "sirius/PluginDescriptor.h"

#include <cstddef>
#include <cstdint>
#include <vector>
```

Then append this test:

```cpp
TEST_CASE ("ChannelStrip<Audio> stores a set-once effect chain + host + node key",
           "[channel-strip][inserts]")
{
    using ida::EffectChain;
    using ida::EffectChainEntry;

    AudioStrip strip;

    // Defaults: no host, empty chain — the pre-Phase-4 configuration.
    CHECK (strip.effectChainHost() == nullptr);
    CHECK (strip.effectChain().empty());

    EffectChainEntry entry;
    entry.descriptor.name = "Comp";
    EffectChain chain;
    chain = chain.withAppended (entry);
    strip.setEffectChain (chain);
    CHECK (strip.effectChain().size() == 1u);

    struct NullHost : ida::IEffectChainHost
    {
        bool pumpSlot (std::int64_t, std::size_t, const float* const*,
                       float* const*, int, int) noexcept override { return false; }
    } host;

    strip.setEffectChainHost (&host, 42);
    CHECK (strip.effectChainHost() == &host);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[channel-strip][inserts]"`
Expected: compile error — `setEffectChain` / `effectChain` / `setEffectChainHost` / `effectChainHost` are not members.

- [ ] **Step 3: Add the collaborators to the header**

In `engine/include/ida/ChannelStrip.h`, add these includes after `#include "sirius/SignalType.h"`:

```cpp
#include "sirius/EffectChain.h"
#include "sirius/IEffectChainHost.h"

#include <cstdint>
```

In the `ChannelStrip<SignalType::Audio>` specialization's `public:` section, add after the `lufsIntegrated()` accessor (just before the `process` docblock):

```cpp
    /// Message-thread setter — copies the insert chain in (routing-graph
    /// Phase 4). Set-once before the audio thread starts; mutating after
    /// start is a threading-contract violation (same collaborator contract
    /// as Bus::setEffectChain).
    void setEffectChain (EffectChain chain) { effectChain_ = std::move (chain); }

    const EffectChain& effectChain() const noexcept { return effectChain_; }

    /// Message-thread setter — wires the audio-thread effect-chain dispatcher
    /// and the node's `pumpSlot` key. The strip does NOT own the host; the
    /// caller owns its lifetime AND guarantees `nodeKey` does not collide
    /// with any other node sharing the same host (see the Phase 4 plan's
    /// key-space note — channels and buses can collide on raw id values).
    /// Pass `nullptr` to disable dispatch — the pre-Phase-4 inline path runs
    /// unchanged. Set-once before the audio thread starts.
    void setEffectChainHost (IEffectChainHost* host, std::int64_t nodeKey) noexcept
    {
        host_    = host;
        nodeKey_ = nodeKey;
    }

    IEffectChainHost* effectChainHost() const noexcept { return host_; }
```

In the `private:` section, add after the `lufsMeter_` member:

```cpp
    // Routing-graph Phase 4 — per-channel insert chain. `effectChain_` is the
    // ordered slot list (copy-in via setEffectChain); `host_` dispatches each
    // non-bypassed slot on the audio thread; `nodeKey_` is the host's
    // `pumpSlot` key for this strip. null `host_` (default) = the pre-Phase-4
    // path, byte-identical. Message-thread set-once; the audio thread only
    // reads.
    EffectChain       effectChain_;
    IEffectChainHost* host_    { nullptr };
    std::int64_t      nodeKey_ { 0 };
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[channel-strip][inserts]"`
Expected: PASS, 1 case.

- [ ] **Step 5: Commit**

```bash
git add engine/include/ida/ChannelStrip.h tests/ChannelStripTests.cpp
git commit -m "feat: ChannelStrip<Audio> set-once EffectChain + host collaborators"
```

---

### Task 3: `ChannelStrip<Audio>::process` dispatches inserts pre-fader (engine)

The dispatch runs after the mute check (a muted strip is silence — no point processing it) and before gain/pan/width/metering, so inserts are **pre-fader** (pro convention) and the post-fader meter reflects the post-insert, post-fader signal. Each non-bypassed slot pumps in-place on the caller's buffer for at most 2 channels (the stereo invariant). When no host is bound or every slot is empty/bypassed, the dispatch is a cheap early-out and the existing DSP body is byte-identical to the pre-Phase-4 code.

**Files:**
- Modify: `engine/include/ida/ChannelStrip.h`
- Test: `tests/ChannelStripTests.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/ChannelStripTests.cpp`. First, in the anonymous namespace area near the top (after the existing `using` declarations), add the mock hosts:

```cpp
namespace
{
    // Applies a different non-commuting op per slot index, proving dispatch
    // visits slots in ascending index order: slot 0 adds 1.0, slot 1 doubles.
    // (in+1)*2 != in*2+1, so the asserted value pins the order.
    struct SlotAwareHost : ida::IEffectChainHost
    {
        bool pumpSlot (std::int64_t nodeKey, std::size_t slotIndex,
                       const float* const* in, float* const* out,
                       int numChannels, int numSamples) noexcept override
        {
            lastNodeKey = nodeKey;
            for (int c = 0; c < numChannels; ++c)
                for (int s = 0; s < numSamples; ++s)
                    out[c][s] = (slotIndex == 0) ? in[c][s] + 1.0f
                                                 : in[c][s] * 2.0f;
            return true;
        }
        std::int64_t lastNodeKey { -1 };
    };

    // Always reports a miss — leaves `out` untouched (the pipelined 1-buffer
    // "dry on miss" case). Records that it was reached.
    struct MissHost : ida::IEffectChainHost
    {
        bool pumpSlot (std::int64_t, std::size_t, const float* const*,
                       float* const*, int, int) noexcept override
        {
            ++calls;
            return false;
        }
        int calls { 0 };
    };

    ida::EffectChain chainOf (std::size_t activeSlots, std::size_t bypassedAtIndex = 999)
    {
        ida::EffectChain chain;
        for (std::size_t i = 0; i < activeSlots; ++i)
        {
            ida::EffectChainEntry e;
            e.descriptor.name = "Fx";
            e.bypassed = (i == bypassedAtIndex);
            chain = chain.withAppended (e);
        }
        return chain;
    }
}
```

Then append the dispatch tests:

```cpp
TEST_CASE ("ChannelStrip<Audio> dispatches inserts pre-fader, in ascending slot order",
           "[channel-strip][inserts]")
{
    AudioStrip strip;
    strip.setGain (1.0f);
    SlotAwareHost host;
    strip.setEffectChainHost (&host, 7);
    strip.setEffectChain (chainOf (2));   // slot 0 (+1) then slot 1 (x2)

    // Mono buffer so pan does not enter — isolates insert + gain.
    std::array<float, 4> mono { 1.0f, 1.0f, 1.0f, 1.0f };
    float* chans[1] = { mono.data() };
    strip.process (chans, 1, 4);

    // (1 + 1) * 2 = 4.0, then gain 1.0. Reversed order would be (1*2)+1 = 3.0.
    for (float v : mono) CHECK (v == Catch::Approx (4.0f));
    CHECK (host.lastNodeKey == 7);        // node key forwarded verbatim
}

TEST_CASE ("ChannelStrip<Audio> inserts run pre-fader (gain applied after the chain)",
           "[channel-strip][inserts]")
{
    AudioStrip strip;
    strip.setGain (0.5f);
    SlotAwareHost host;
    strip.setEffectChainHost (&host, 1);
    strip.setEffectChain (chainOf (1));   // slot 0 (+1) only

    std::array<float, 2> mono { 1.0f, 1.0f };
    float* chans[1] = { mono.data() };
    strip.process (chans, 1, 2);

    // Pre-fader: (1 + 1) * 0.5 = 1.0. Post-fader would be (1*0.5)+1 = 1.5.
    for (float v : mono) CHECK (v == Catch::Approx (1.0f));
}

TEST_CASE ("ChannelStrip<Audio> skips bypassed insert slots", "[channel-strip][inserts]")
{
    AudioStrip strip;
    strip.setGain (1.0f);
    SlotAwareHost host;
    strip.setEffectChainHost (&host, 1);
    strip.setEffectChain (chainOf (2, /*bypassedAtIndex*/ 0)); // slot 0 bypassed, slot 1 (x2)

    std::array<float, 2> mono { 1.0f, 1.0f };
    float* chans[1] = { mono.data() };
    strip.process (chans, 1, 2);

    // Only slot 1 (x2) runs: 1.0 * 2 = 2.0. If slot 0 ran it would be (1+1)*2=4.
    for (float v : mono) CHECK (v == Catch::Approx (2.0f));
}

TEST_CASE ("ChannelStrip<Audio> a pumpSlot miss leaves the dry signal unchanged",
           "[channel-strip][inserts]")
{
    AudioStrip strip;
    strip.setGain (1.0f);
    MissHost host;
    strip.setEffectChainHost (&host, 1);
    strip.setEffectChain (chainOf (1));

    std::array<float, 2> mono { 0.3f, 0.3f };
    float* chans[1] = { mono.data() };
    strip.process (chans, 1, 2);

    CHECK (host.calls == 1);                          // the slot WAS dispatched
    for (float v : mono) CHECK (v == Catch::Approx (0.3f)); // miss => dry carries
}

TEST_CASE ("ChannelStrip<Audio> behavior-equivalent to gain-only when no host / empty / all-bypassed",
           "[channel-strip][inserts]")
{
    SlotAwareHost host;

    // (a) host bound but chain empty.
    {
        AudioStrip strip;
        strip.setGain (2.0f);
        strip.setEffectChainHost (&host, 1);          // empty chain
        std::array<float, 2> mono { 0.25f, 0.25f };
        float* chans[1] = { mono.data() };
        strip.process (chans, 1, 2);
        for (float v : mono) CHECK (v == Catch::Approx (0.5f)); // gain only
    }

    // (b) chain present but all slots bypassed.
    {
        AudioStrip strip;
        strip.setGain (2.0f);
        strip.setEffectChainHost (&host, 1);
        strip.setEffectChain (chainOf (1, /*bypassedAtIndex*/ 0));
        std::array<float, 2> mono { 0.25f, 0.25f };
        float* chans[1] = { mono.data() };
        strip.process (chans, 1, 2);
        for (float v : mono) CHECK (v == Catch::Approx (0.5f)); // gain only
    }

    // (c) chain present but no host bound.
    {
        AudioStrip strip;
        strip.setGain (2.0f);
        strip.setEffectChain (chainOf (1));           // no setEffectChainHost
        std::array<float, 2> mono { 0.25f, 0.25f };
        float* chans[1] = { mono.data() };
        strip.process (chans, 1, 2);
        for (float v : mono) CHECK (v == Catch::Approx (0.5f)); // gain only
    }
}

TEST_CASE ("ChannelStrip<Audio> a muted strip skips inserts entirely", "[channel-strip][inserts]")
{
    AudioStrip strip;
    strip.setMuted (true);
    MissHost host;
    strip.setEffectChainHost (&host, 1);
    strip.setEffectChain (chainOf (1));

    std::array<float, 2> mono { 1.0f, 1.0f };
    float* chans[1] = { mono.data() };
    strip.process (chans, 1, 2);

    CHECK (host.calls == 0);                          // muted => no dispatch
    for (float v : mono) CHECK (v == Catch::Approx (0.0f));
}

TEST_CASE ("ChannelStrip<Audio> meter reflects the post-insert, post-fader signal",
           "[channel-strip][inserts]")
{
    AudioStrip strip;
    strip.setGain (1.0f);
    SlotAwareHost host;
    strip.setEffectChainHost (&host, 1);
    strip.setEffectChain (chainOf (1));               // slot 0 (+1)

    std::array<float, 2> mono { 0.5f, 0.5f };
    float* chans[1] = { mono.data() };
    strip.process (chans, 1, 2);

    // 0.5 + 1 = 1.5; gain 1.0; mono => same peak on both meter sides.
    CHECK (strip.peakLeft()  == Catch::Approx (1.5f));
    CHECK (strip.peakRight() == Catch::Approx (1.5f));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[channel-strip][inserts]"`
Expected: the new dispatch cases FAIL — `process` does not call the host yet, so e.g. the ascending-order case sees `1.0` (gain-only) instead of `4.0`. (The Task 2 storage case still passes.)

- [ ] **Step 3: Add the dispatch helper + call it in `process`**

In `engine/include/ida/ChannelStrip.h`, add a private helper. Place it in the `private:` section of the `ChannelStrip<SignalType::Audio>` specialization, after the new `nodeKey_` member:

```cpp
    /// Stereo invariant — inserts dispatch at most two channels.
    static constexpr int kMaxInsertChannels = 2;

    /// Audio-thread per-channel insert dispatch (routing-graph Phase 4).
    /// Runs each non-bypassed slot in ascending index order, in-place on
    /// `channelData` (in == out; the host contractually reads all input
    /// before writing any output). Early-out — and therefore byte-identical
    /// to the pre-Phase-4 body — when no host is bound or every slot is
    /// empty/bypassed. `noexcept`, allocation-free, lock-free: it only reads
    /// `host_`/`nodeKey_`/`effectChain_` (message-thread set-once) and calls
    /// the host's `noexcept` `pumpSlot`.
    void dispatchInserts (float* const* channelData, int numChannels,
                          int numSamples) const noexcept
    {
        if (host_ == nullptr) return;

        const auto& entries = effectChain_.entries();
        bool hasActiveSlot = false;
        for (const auto& e : entries)
            if (! e.bypassed) { hasActiveSlot = true; break; }
        if (! hasActiveSlot) return;

        const int insertChannels = std::min (numChannels, kMaxInsertChannels);
        for (std::size_t slotIdx = 0; slotIdx < entries.size(); ++slotIdx)
        {
            if (entries[slotIdx].bypassed) continue;
            // In-place: in and out both point at the caller's buffer. On a
            // miss (false), pumpSlot leaves channelData unchanged — the dry
            // signal carries to the next slot / the fader (1-buffer-delay).
            (void) host_->pumpSlot (nodeKey_, slotIdx,
                                    channelData, channelData,
                                    insertChannels, numSamples);
        }
    }
```

Then, inside `process`, add the dispatch call **after the muted early-return block and before `const float g = gainLinear_.load (...)`**. The relevant region currently reads:

```cpp
            return;
        }

        const float g = gainLinear_.load (std::memory_order_relaxed);
```

Change it to:

```cpp
            return;
        }

        // Routing-graph Phase 4 — inserts run pre-fader (before gain/pan/
        // width) so the post-fader meter reflects the post-insert signal.
        // Inert (byte-identical to the pre-Phase-4 body) when no host is
        // bound or every slot is empty/bypassed.
        dispatchInserts (channelData, numChannels, numSamples);

        const float g = gainLinear_.load (std::memory_order_relaxed);
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --target IdaTests && ./build/tests/IdaTests "[channel-strip][inserts]"`
Expected: PASS — all `[channel-strip][inserts]` cases (the Task 2 storage case + the 7 dispatch cases).

- [ ] **Step 5: Confirm RT-safety static-assert + no channel-strip regression**

The header's existing `static_assert` on `process` being `noexcept` must still hold (compilation proves it). Run the full channel-strip tag:

Run: `./build/tests/IdaTests "[channel-strip]"`
Expected: PASS (all pre-existing gain/pan/width/lufs cases + the new insert cases).

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/ChannelStrip.h tests/ChannelStripTests.cpp
git commit -m "feat: ChannelStrip<Audio> pre-fader insert dispatch via IEffectChainHost"
```

---

### Task 4: Verify whole suite + handoff (continue.md, todo.md)

The mandatory per-phase handoff (spec "MANDATORY per-phase handoff"; memory `feedback_update_continue_md_every_session`). A clean rebuild proves no stale-config drift, the full ctest pins the count, and `continue.md` is rewritten so the next chat resumes Phase 5 from "read continue.md" alone.

**Files:**
- Modify: `continue.md`
- Modify: `todo.md`

- [ ] **Step 1: Clean rebuild + full suite**

Run:
```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests
ctest --test-dir build
```
Expected: build green; ctest passes with **one** documented non-pass (`MainComponentPluginEditorTests_NOT_BUILT`, run separately by `bash/test-s7.sh`). Record the exact `N/N+1` count for the handoff. (Phase 3 was 495/496; the new cases push the numerator up — record the actual number printed.)

- [ ] **Step 2: Append the Phase 4 deferrals to `todo.md`**

Add a dated entry capturing: (1) production wiring deferred to UI Phases 6–7 (mixer owns an `OutOfProcessEffectChainHost`, allocates a non-colliding channel/bus `int64` key space, calls `configureBus` per channel); (2) the `configureBus` → `configureNode` rename as a future tidy; (3) built-in IDA FX as slot contents = the follow-on spec. Use the exact `### [Date] - [Task]` / Files / What was deferred / Why / What's needed format from `~/.claude/CLAUDE.md`.

- [ ] **Step 3: Rewrite the `continue.md` RESUME block for Phase 5**

Replace the top "RESUME HERE" block. It must state: Phase 4 shipped (the 3 feat commits + this handoff), the exact ctest count from Step 1, clean-rebuild status, and that **Phase 5 — routing-graph persistence** is next. Include Phase 5's first moves: read the spec's Phase 5 section (~lines 280-284) + `persistence/.../SessionFormat.*` (how chains/buses already serialize) + `tests/SessionFormatTests.cpp`; then `superpowers:writing-plans` → `docs/superpowers/plans/2026-05-20-mixer-routing-graph-phase5.md`; engine+persistence TDD; cover round-trip equality + forward-compat load of pre-graph sessions for both mixers. Demote the current Phase 3 block to HISTORICAL. Keep the locked-design-decisions and operational-facts sections.

- [ ] **Step 4: Commit + push**

```bash
git add continue.md todo.md
git commit -m "docs: continue.md + todo.md — routing Phase 4 (per-node insert chains) shipped, next = Phase 5 persistence"
git push origin master
```
(Push is authorized per memory `feedback_claude_commits_and_pushes_master`.)

---

## Self-Review

**1. Spec coverage (Phase 4 acceptance: "per-channel chain dispatch, 8-slot enforcement, bypass/reorder, RT-safety, behavior-equivalence for empty chains"):**
- per-channel chain dispatch → Task 3 (ascending-order, node-key-forwarded cases).
- 8-slot enforcement (buses/returns too) → Task 1 (cap on the shared `EffectChain` type binds all three node kinds).
- bypass → Task 3 (skips-bypassed-slots case).
- reorder → `EffectChain::withMoved` already exists and is tested in `EffectChainTests.cpp`; Task 3 proves `process` dispatches in `entries()` order (the ascending-order case), so reordering entries reorders dispatch. No new reorder code is needed; noted rather than re-implemented.
- RT-safety → Task 3 Step 5 (the header `noexcept` static-assert holds; `dispatchInserts` allocates nothing and is lock-free).
- behavior-equivalence for empty chains → Task 3 (no-host / empty / all-bypassed case, all assert gain-only output).
- "external VST/CLAP via the existing M7 out-of-process host" → satisfied by the unchanged `int64`-keyed host; apparatus proven with the mock, production wiring deferred (Scope section + Task 4 todo). Consistent with the Phase-3 apparatus posture the handoff documents.

**2. Placeholder scan:** every code step shows complete code; every command shows expected output. No TBD/TODO/"handle edge cases". Task 2 and Task 4 reference only types/methods defined here or already in the tree (`makeEntry`, `AudioStrip`, `EffectChainEntry.descriptor.name`, `EffectChain.withAppended/withMoved/withRemoved/withReplaced`, `IEffectChainHost::pumpSlot`).

**3. Type consistency:** `kMaxSlots` (Task 1) used in Task 1 tests; `setEffectChain`/`effectChain`/`setEffectChainHost`/`effectChainHost`/`nodeKey_`/`host_`/`effectChain_`/`dispatchInserts`/`kMaxInsertChannels` consistent across Tasks 2–3; `setEffectChainHost(host, nodeKey)` two-arg signature used identically in every test and the impl. `pumpSlot` signature matches `IEffectChainHost` (`std::int64_t, std::size_t, const float* const*, float* const*, int, int`) in all three mock hosts.
