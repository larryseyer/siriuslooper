# Slice 8 — Phrase-Trigger Button Bank — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** A persistent horizontal row directly beneath the top-bar transport area — `< 1 2 3 4 5 6 7 8 >` — that gives every phrase a fixed, paged, colour-coded launcher. Pressing an active button drives *that phrase's* per-phrase state machine through Slice 5's source-agnostic command layer (the bank is just one more input source, not a parallel capture path). Empty slots are inert. A single right-click / long-press context menu offers Clear / Copy / Paste / Rename / Assign MIDI…. Buttons default to numeric labels, are renameable, and each stores a MIDI note+channel+port binding for future live triggering. Spec §8.5.

**Architecture:** Three layers, mirroring the repo's "engine is headless-testable, GUI is operator-verified" split:

1. **Pure helpers (headless, in `IdaUi`)** — a JUCE-free-logic header `PhraseButtonBankModel` providing: the positional bank math `phraseSlotIndex(bank, position) = (bank-1)*8 + position`; the inverse `(slot → {bank, position})`; `pageCount(phraseCount)`; and the **state→colour** mapping `slotColour(SlotView)` that resolves the traditional looper STATE colour (empty=dim, rec=red, play=green, overdub=amber, stopped=dim) reconciled with the per-phrase IDENTITY hue from the colour method (`ida::palette::phraseColour`). These are free functions + small POD structs — fully Catch2-testable.

2. **The bank Component (`PhraseButtonBank`, in `IdaUi`)** — owns 8 `PhraseSlotButton`s + two chevron buttons, lays them out as one row, paints each button from its `SlotView`, dispatches a *single* `onTrigger(slot)` / `onContextMenu(slot, screenPos)` / `onCommitRename(slot, name)` callback set, and pages banks via the chevrons. It owns **no** session/engine state — `MainComponent` pushes `std::vector<SlotView>` for the current bank and the bank renders it. Reuses the canonical right-click + 500 ms long-press gesture pattern already proven in `app/StripContextOverlay.h` (right-click and long-press are the SAME gesture — iOS has only long-press).

3. **MainComponent wiring (in `app/`)** — places the bank in `resized()` directly under `transportBarHost_`; builds the current-bank `SlotView` list by enumerating phrases the same way `refreshOutputMixerPhraseChannels()` does (the `selectTimelineView(...).pills` DFS order, positional and stable); maps each button's `onTrigger` to **Slice 5's command layer** (`ICaptureCommandSink::dispatch(PhraseTrigger{constituentId})`); builds the context menu with `juce::PopupMenu` (the IDA-side convention — `StripContextOverlay` / pane menus use it; the OTTO `TouchMenuPresenter` rule is OTTO-internal only); and persists per-button MIDI bindings.

**Cross-slice contract (Slice 5 must expose; this plan defines the seam it consumes).** Slice 5's plan does not exist yet (the roadmap detail-plans it at execution). Slice 8 needs two things from Slice 5, both small and source-agnostic per spec §8.4. To keep Slice 8 testable and unblocked, **define the seam as a pure interface in `core`** that Slice 5 implements; the bank and its tests depend only on the interface + a fake:

- **State read:** a per-phrase looper state enum + a query. Add `enum class PhraseLooperState { Empty, Recording, Playing, Overdub, Stopped };` and `PhraseLooperState looperStateFor(ConstituentId) const` on an abstract `IPhraseStateSource`. (Slice 5 owns the real mapping from its combined-state ids 0–6 to these five glanceable buckets; Slice 8 only consumes the five.)
- **Command write:** the source-agnostic dispatch. Add an abstract `IPhraseCommandSink` with `void dispatchPhraseTrigger(ConstituentId)` (advance that phrase's state — play/stop/overdub per its current state, exactly the §8.5 "press = drive that phrase's multi-mode state"). A finger, a MIDI note, and the bank button all call this one method — the same command object spec §8.4 mandates.

> **Risk / dependency the roadmap understates:** the roadmap lists Slice 8 as depending on Slices 5 + 6 but does **not** name the concrete API surface. If Slice 5 lands without `IPhraseStateSource` / `IPhraseCommandSink` (or equivalents), Slice 8's MainComponent wiring (Tasks 6–8) is blocked. This plan therefore (a) defines those two interfaces in `core` as the contract, and (b) keeps the bank Component + all headless tests dependent only on the interfaces + a `FakePhraseSession`, so the bank is fully buildable and TDD-able the moment this slice starts, and the MainComponent wiring is a thin adapter once Slice 5's concrete types exist. **HARD-STOP rule:** if at Task 6 the real Slice 5 command/state types are absent, write the `## HALT` entry per `~/.claude/CLAUDE.md` and stop — do not invent a parallel capture path (the spec forbids it).

**Tech Stack:** C++20 / JUCE; new code in `IdaUi` (`ui/include/ida/`, `ui/src/`) and `app/`; the two seam interfaces in `core/include/ida/`; Catch2 (`IdaTests`); CMake + Ninja. Colours: `ui/include/ida/IdaPalette.h` + `ui/lookandfeel/OTTOColours.h` (consumed via `IdaLookAndFeel`). Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` §8.5.

**Dependencies:**
- **Slice 5** — per-phrase state machine + source-agnostic command layer (this plan defines the `IPhraseStateSource` / `IPhraseCommandSink` seam Slice 5 implements).
- **Slice 6** — "play" plays all of a phrase's loops (so the green/Playing state means what §8.5 says). Slice 8 does not call Slice 6 code directly; it relies on the behaviour being present.
- Existing, already-landed: `ida::palette::phraseColour` (`ui/include/ida/IdaPalette.h`); `otto::Colours` semantic colours (`ui/lookandfeel/OTTOColours.h`); the right-click+long-press gesture pattern (`app/StripContextOverlay.h`); phrase enumeration via `selectTimelineView(...).pills` (`ui/include/ida/TimelineViewState.h`, used at `app/MainComponent.cpp:7105`); `UndoStack` (`ui/include/ida/UndoStack.h`); transport-bar placement seam (`app/MainComponent.cpp` `resized()` ~6127, `kTransportBarDesktopHeightPx` = 88 at `app/MainComponent.cpp:129`).

---

## Files

**Create:**
- `core/include/ida/PhraseLooperState.h` — `enum class PhraseLooperState`, `struct IPhraseStateSource`, `struct IPhraseCommandSink` (the Slice 5 seam; header-only abstract interfaces, JUCE-free).
- `core/include/ida/PhraseMidiBinding.h` — `struct PhraseMidiBinding { int note; int channel; std::string port; bool assigned; }` (JUCE-free POD; persistence-ready).
- `ui/include/ida/PhraseButtonBankModel.h` — pure helpers: `slotIndexFor(bank,pos)`, `slotToBankPosition(slot)`, `pageCount(phraseCount)`, `struct SlotView`, `slotColour(SlotView)`. (Uses `juce::Colour` + `ida::palette`, so it lives in `IdaUi`, but every function is pure.)
- `ui/include/ida/PhraseButtonBank.h` + `ui/src/PhraseButtonBank.cpp` — the Component (8 buttons + 2 chevrons + paging + gesture + rename inline editor).
- `tests/PhraseButtonBankModelTests.cpp` — Catch2 for the pure helpers + state→colour map + slot inertness + the command-dispatch-equivalence proof.

**Modify:**
- `ui/CMakeLists.txt` (`IdaUi` target ~90–104) — add `src/PhraseButtonBank.cpp`.
- `tests/CMakeLists.txt` (`IdaTests` target ~1) — add `PhraseButtonBankModelTests.cpp`.
- `app/MainComponent.h` — declare `std::unique_ptr<ida::PhraseButtonBank> phraseBank_;`, an `int phraseBankPage_ { 0 };` cursor, the per-constituent MIDI-binding map `std::unordered_map<std::int64_t, ida::PhraseMidiBinding> phraseMidiBindings_;`, a clipboard `std::optional<ida::ConstituentId> phraseCopyBuffer_;`, and method decls `void refreshPhraseBank();`, `void onPhraseButtonTrigger(int slotInBank);`, `void showPhraseButtonMenu(int slotInBank, juce::Point<int>);`, `void onCommitPhraseButtonRename(int slotInBank, juce::String);`.
- `app/MainComponent.cpp` — construct `phraseBank_`, dock it in `resized()` under the transport bar, wire the callbacks, call `refreshPhraseBank()` from the same places `refreshOutputMixerPhraseChannels()` is called, and add a static `kPhraseBankHeightPx` next to `kTransportBarDesktopHeightPx` (~129).

**Test:** `tests/PhraseButtonBankModelTests.cpp` (headless). GUI behaviour (layout, paging, colours, menu, rename) is operator-verified per repo convention.

---

## Tasks

### Task 1 — The Slice-5 seam interfaces + MIDI-binding POD (core, headless contract)

**Files:** Create `core/include/ida/PhraseLooperState.h`, `core/include/ida/PhraseMidiBinding.h`.

- [ ] **Step 1** Write `core/include/ida/PhraseLooperState.h` — the abstract seam Slice 5 implements and Slice 8 consumes. JUCE-free, header-only:

```cpp
#pragma once

#include "ida/ConstituentId.h"

namespace ida
{

/// The five glanceable looper states a phrase button paints, in the universal
/// looper colour language (spec §8.5). Slice 5 owns the mapping from its
/// combined-state ids (0–6, spec §8) onto these five buckets; the button bank
/// consumes only this enum.
///   Empty     — no phrase exists at this slot (inert button).
///   Recording — the phrase / its first loop is being defined (red).
///   Playing   — the phrase is playing all its loops (green).
///   Overdub   — a new loop is being layered onto the phrase (amber).
///   Stopped   — the phrase exists, loaded but not playing (dim).
enum class PhraseLooperState
{
    Empty,
    Recording,
    Playing,
    Overdub,
    Stopped
};

/// Read seam: the bank asks "what colour state is phrase C in?". Implemented
/// by Slice 5's per-phrase state machine. A constituent id with no phrase
/// returns Empty (so the bank's empty-slot rule and a stale id both render
/// inert).
struct IPhraseStateSource
{
    virtual ~IPhraseStateSource() = default;
    virtual PhraseLooperState looperStateFor (ConstituentId phrase) const = 0;
};

/// Write seam: the single source-agnostic command (spec §8.4). A finger on a
/// button, a mapped MIDI note, and a footswitch ALL call this one method —
/// advancing THAT phrase's multi-mode state (play / stop / overdub) per its
/// current state. The bank does not know or branch on the current state; the
/// state machine resolves the action. There is exactly one capture code path.
struct IPhraseCommandSink
{
    virtual ~IPhraseCommandSink() = default;
    virtual void dispatchPhraseTrigger (ConstituentId phrase) = 0;
};

} // namespace ida
```

- [ ] **Step 2** Write `core/include/ida/PhraseMidiBinding.h` — a persistence-ready POD for the per-button MIDI assignment (storage now; live triggering is future §14):

```cpp
#pragma once

#include <string>

namespace ida
{

/// A per-phrase MIDI trigger binding captured by the Assign-MIDI dialog
/// (spec §8.5). Stored now; live MIDI triggering is future work (§14). Kept a
/// JUCE-free POD so persistence and the (future) MIDI router both consume it.
///   note    — MIDI note number 0–127 (or -1 when unassigned).
///   channel — 1–16 (MIDI convention; 0 == "omni / any").
///   port    — input device name; empty == "any port".
struct PhraseMidiBinding
{
    int         note    { -1 };
    int         channel { 0 };
    std::string port    {};

    bool assigned() const noexcept { return note >= 0 && note <= 127; }
};

} // namespace ida
```

- [ ] **Step 3** Verify the headers compile standalone (no stray deps). Run:
`cmake --build build --target IdaTests 2>&1 | head -30`
Expected: no errors referencing the two new headers (they are not yet `#include`d anywhere, so this just confirms the tree still builds). If `build/` does not exist yet, configure first: `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release`.

---

### Task 2 — Pure bank model: positional math + paging (TDD, headless)

**Files:** Create `ui/include/ida/PhraseButtonBankModel.h`; create `tests/PhraseButtonBankModelTests.cpp`; modify `tests/CMakeLists.txt`.

REQUIRED SUB-SKILL: superpowers:test-driven-development — write the failing test first.

- [ ] **Step 1** Register the test file. In `tests/CMakeLists.txt`, in the `add_executable(IdaTests …)` source list (after `TimelineViewStateTests.cpp` at line ~51), add:
```cmake
    PhraseButtonBankModelTests.cpp
```

- [ ] **Step 2** Write the failing positional-math test in `tests/PhraseButtonBankModelTests.cpp`:

```cpp
#include "ida/PhraseButtonBankModel.h"

#include <catch2/catch_test_macros.hpp>

using namespace ida;

TEST_CASE ("slot index is positional: (bank-1)*8 + position", "[phrase-bank]")
{
    // Banks are 1-based, positions 1..8 (matching the on-screen "1 2 .. 8").
    CHECK (slotIndexFor (1, 1) == 0);    // bank 1, button 1 -> phrase index 0
    CHECK (slotIndexFor (1, 8) == 7);    // bank 1, button 8 -> phrase index 7
    CHECK (slotIndexFor (2, 1) == 8);    // bank 2, button 1 -> phrase index 8
    CHECK (slotIndexFor (2, 8) == 15);
    CHECK (slotIndexFor (3, 4) == 19);   // (3-1)*8 + 4
}

TEST_CASE ("slot index round-trips to bank+position", "[phrase-bank]")
{
    for (int slot = 0; slot < 40; ++slot)
    {
        const auto bp = slotToBankPosition (slot);
        CHECK (bp.bank >= 1);
        CHECK (bp.position >= 1);
        CHECK (bp.position <= 8);
        CHECK (slotIndexFor (bp.bank, bp.position) == slot);  // inverse holds
    }
}

TEST_CASE ("page count covers the unbounded phrase list eight at a time", "[phrase-bank]")
{
    CHECK (pageCount (0)  == 1);   // always at least one (empty) page
    CHECK (pageCount (1)  == 1);
    CHECK (pageCount (8)  == 1);
    CHECK (pageCount (9)  == 2);
    CHECK (pageCount (16) == 2);
    CHECK (pageCount (17) == 3);
}
```

- [ ] **Step 3** Write `ui/include/ida/PhraseButtonBankModel.h` with ONLY the math (colour comes in Task 3) so this compiles:

```cpp
#pragma once

#include "ida/ConstituentId.h"
#include "ida/PhraseLooperState.h"

namespace ida
{

inline constexpr int kPhraseBankButtons = 8;

/// Positional slot index for 1-based bank + 1-based position (spec §8.5):
/// button position p in bank b ↔ phrase index (b-1)*8 + p, returned 0-based.
inline int slotIndexFor (int bank, int position) noexcept
{
    return (bank - 1) * kPhraseBankButtons + (position - 1);
}

struct BankPosition { int bank; int position; };  // both 1-based

/// Inverse of slotIndexFor for a 0-based slot index.
inline BankPosition slotToBankPosition (int slot) noexcept
{
    return { slot / kPhraseBankButtons + 1, slot % kPhraseBankButtons + 1 };
}

/// Number of 8-button pages needed for `phraseCount` phrases (≥1 always, so an
/// empty session still shows one page of inert buttons).
inline int pageCount (int phraseCount) noexcept
{
    if (phraseCount <= 0) return 1;
    return (phraseCount + kPhraseBankButtons - 1) / kPhraseBankButtons;
}

} // namespace ida
```

- [ ] **Step 4** Build + run, expect PASS:
`cmake --build build --target IdaTests && ctest --test-dir build -R phrase-bank`
Expected: the three cases pass.

---

### Task 3 — State→colour map + empty-slot inertness (TDD, headless)

**Files:** Modify `ui/include/ida/PhraseButtonBankModel.h`, `tests/PhraseButtonBankModelTests.cpp`.

The traditional looper STATE colour is reconciled with the per-phrase IDENTITY hue from the colour method: an **active** button is tinted toward its phrase's stable identity hue (`palette::phraseColour`) but the STATE drives the dominant signal — Recording/Playing/Overdub use the universal red/green/amber so the state is glanceable; Stopped shows a dim wash of the identity hue (phrase still recognisable, clearly "loaded but idle"); Empty is the neutral dim with no identity. This is the §8.5 "how state colour reconciles with identity colour is settled with the palette during implementation" decision.

- [ ] **Step 1** Add the failing colour + inertness test cases to `tests/PhraseButtonBankModelTests.cpp`:

```cpp
TEST_CASE ("empty slots are inert: Empty state renders the neutral dim, no identity", "[phrase-bank]")
{
    SlotView empty;                         // default: no phrase, Empty state
    CHECK_FALSE (empty.hasPhrase);
    const auto c = slotColour (empty);
    CHECK (c == otto::Colours::transportInactive);   // the universal "off/dim"
}

TEST_CASE ("recording is red, playing is green, overdub is amber", "[phrase-bank]")
{
    SlotView s;
    s.hasPhrase  = true;
    s.phraseId   = ConstituentId { 3 };

    s.state = PhraseLooperState::Recording;
    CHECK (slotColour (s) == otto::Colours::error);     // red

    s.state = PhraseLooperState::Playing;
    CHECK (slotColour (s) == otto::Colours::success);   // green

    s.state = PhraseLooperState::Overdub;
    CHECK (slotColour (s) == otto::Colours::warning);   // amber
}

TEST_CASE ("a stopped (loaded) phrase shows a dim wash of its identity hue", "[phrase-bank]")
{
    SlotView s;
    s.hasPhrase = true;
    s.phraseId  = ConstituentId { 5 };
    s.state     = PhraseLooperState::Stopped;

    // Identity-keyed, not state-keyed: two different stopped phrases differ,
    // and each is a darkened form of its own phraseColour (recognisable but
    // clearly idle), never the bright play/rec/overdub signal.
    const auto stoppedFive = slotColour (s);
    s.phraseId = ConstituentId { 6 };
    const auto stoppedSix  = slotColour (s);
    CHECK (stoppedFive != stoppedSix);                                   // identity carries
    CHECK (stoppedFive == ida::palette::phraseColour (5).withMultipliedBrightness (0.45f));
    CHECK (stoppedFive != otto::Colours::success);                      // not the play colour
}
```

- [ ] **Step 2** Extend `ui/include/ida/PhraseButtonBankModel.h` with `SlotView` + `slotColour`, adding the needed includes (`IdaPalette.h` pulls `OTTOColours.h`):

```cpp
#include "ida/IdaPalette.h"   // brings juce::Colour, ida::palette, otto::Colours
```

```cpp
/// What one button needs to render itself. MainComponent fills a vector of
/// these for the visible bank (8 entries) and pushes it to PhraseButtonBank.
/// A slot with no phrase (hasPhrase == false) is INERT — it paints empty and
/// its trigger does nothing (spec §8.5: these buttons trigger phrases, never
/// create them).
struct SlotView
{
    bool              hasPhrase { false };
    ConstituentId     phraseId  { 0 };
    PhraseLooperState state     { PhraseLooperState::Empty };
    /// The button label. Default is the 1-based phrase number as text; a
    /// rename replaces it (cosmetic only — positional mapping is unchanged).
    juce::String      label;
};

/// The traditional looper STATE colour, reconciled with the phrase IDENTITY
/// hue (spec §8.5). State drives the dominant signal; Stopped carries identity
/// as a dim wash; Empty is the neutral dim.
inline juce::Colour slotColour (const SlotView& slot) noexcept
{
    if (! slot.hasPhrase) return otto::Colours::transportInactive;     // inert dim

    switch (slot.state)
    {
        case PhraseLooperState::Recording: return otto::Colours::error;    // red
        case PhraseLooperState::Playing:   return otto::Colours::success;  // green
        case PhraseLooperState::Overdub:   return otto::Colours::warning;  // amber
        case PhraseLooperState::Stopped:
            // Recognisable-but-idle: a darkened form of the phrase's stable hue.
            return ida::palette::phraseColour (slot.phraseId.value())
                       .withMultipliedBrightness (0.45f);
        case PhraseLooperState::Empty:
        default:
            return otto::Colours::transportInactive;
    }
}
```

- [ ] **Step 3** Build + run, expect PASS:
`cmake --build build --target IdaTests && ctest --test-dir build -R phrase-bank`
Expected: all Task 2 + Task 3 cases pass. (If `ConstituentId::value()` isn't visible, confirm `ConstituentId.h` is included — it is, transitively, via `PhraseLooperState.h`.)

---

### Task 4 — Command-dispatch equivalence: a button press == a MIDI press (TDD, headless)

This is the spec §8.4 / §8.5 invariant: the bank is one more input source, not a separate code path. We prove it by driving a `FakePhraseSession` (implements both seams) through the *same* `dispatchPhraseTrigger` the bank uses, from two different "sources", and asserting identical effect.

**Files:** Modify `tests/PhraseButtonBankModelTests.cpp`.

- [ ] **Step 1** Add the failing equivalence test (the helper `triggerSlot` is the exact function the bank Component will call in Task 5, kept pure here so it is unit-testable without a Component):

```cpp
#include "ida/PhraseLooperState.h"
#include <vector>

namespace
{
/// A stand-in for Slice 5's session: records every trigger and lets a test
/// script per-phrase state. Implements BOTH seams the bank depends on.
struct FakePhraseSession : ida::IPhraseStateSource, ida::IPhraseCommandSink
{
    std::vector<ida::ConstituentId> triggered;
    ida::PhraseLooperState          scripted { ida::PhraseLooperState::Stopped };

    ida::PhraseLooperState looperStateFor (ida::ConstituentId) const override { return scripted; }
    void dispatchPhraseTrigger (ida::ConstituentId p) override { triggered.push_back (p); }
};
}

TEST_CASE ("an inert (empty) slot dispatches NOTHING when triggered", "[phrase-bank]")
{
    FakePhraseSession session;
    SlotView empty;                                   // hasPhrase == false
    triggerSlot (empty, session);                     // the bank's press handler
    CHECK (session.triggered.empty());                // empty slots never create/trigger
}

TEST_CASE ("a finger press and a MIDI press dispatch the identical command", "[phrase-bank]")
{
    FakePhraseSession fromFinger;
    FakePhraseSession fromMidi;

    SlotView s;
    s.hasPhrase = true;
    s.phraseId  = ConstituentId { 42 };

    // "Finger" path: the bank button's onTrigger -> triggerSlot.
    triggerSlot (s, fromFinger);
    // "MIDI" path: a future MIDI router resolves the same slot -> same call.
    triggerSlot (s, fromMidi);

    REQUIRE (fromFinger.triggered.size() == 1);
    REQUIRE (fromMidi.triggered.size()   == 1);
    CHECK (fromFinger.triggered.front() == ConstituentId { 42 });
    CHECK (fromMidi.triggered.front()   == fromFinger.triggered.front());  // identical
}
```

- [ ] **Step 2** Add `triggerSlot` to `ui/include/ida/PhraseButtonBankModel.h` — the single press-handling function (the Component calls it; the test calls it; a MIDI router will call it):

```cpp
/// The one press handler. Inert slots do nothing; active slots dispatch the
/// single source-agnostic command (spec §8.4). Keeping this a free function
/// means the GUI button, the test, and a future MIDI router all share one
/// definition — there is provably one capture path.
inline void triggerSlot (const SlotView& slot, IPhraseCommandSink& sink)
{
    if (! slot.hasPhrase) return;            // empty slot is inert
    sink.dispatchPhraseTrigger (slot.phraseId);
}
```

- [ ] **Step 3** Build + run, expect PASS:
`cmake --build build --target IdaTests && ctest --test-dir build -R phrase-bank`
Expected: all phrase-bank cases pass.

- [ ] **Step 4** Commit the headless foundation:
```bash
git add core/include/ida/PhraseLooperState.h core/include/ida/PhraseMidiBinding.h \
        ui/include/ida/PhraseButtonBankModel.h tests/PhraseButtonBankModelTests.cpp tests/CMakeLists.txt
git commit -m "feat: phrase-button-bank model — positional math, state colour, source-agnostic trigger (headless)"
```
(Single-line message; trailer auto-applied: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.)

---

### Task 5 — The PhraseButtonBank Component (8 buttons + chevrons + paging + gesture + rename)

**Files:** Create `ui/include/ida/PhraseButtonBank.h`, `ui/src/PhraseButtonBank.cpp`; modify `ui/CMakeLists.txt`.

This is GUI — no headless unit test (per repo convention); operator-verified in Task 9. Reuse the right-click + 500 ms long-press gesture from `app/StripContextOverlay.h` (it is the SAME gesture; iOS has only long-press) and its inline-rename TextEditor pattern.

- [ ] **Step 1** Register the source in `ui/CMakeLists.txt` `IdaUi` list (after `src/TimelineView.cpp`, line ~98):
```cmake
    src/PhraseButtonBank.cpp
```

- [ ] **Step 2** Write `ui/include/ida/PhraseButtonBank.h`. The Component owns 8 slot buttons + two chevrons, holds no session state, and relays via callbacks. `setSlots(...)` pushes the visible bank; `setPage(...)` / `pageCount` drive paging:

```cpp
#pragma once

#include "ida/PhraseButtonBankModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace ida
{

/// The persistent phrase-trigger button bank (spec §8.5): a single horizontal
/// row `< 1 2 3 4 5 6 7 8 >` docked directly beneath the top-bar transport
/// area. Eight numbered buttons flanked by left/right chevrons that page banks
/// of 8. The Component is a pure VIEW — MainComponent owns the session, fills
/// a vector<SlotView> for the current page, and pushes it via setSlots().
///
/// Gestures (mirroring app/StripContextOverlay): a short tap = trigger; a
/// right-click (desktop) OR 500 ms long-press (iOS — the SAME gesture) = the
/// context menu; chevrons page. Inert (empty) buttons paint dim and swallow
/// neither a trigger nor a menu beyond the slot-context relay (MainComponent
/// decides — an empty slot's menu has nothing actionable, see Task 7).
class PhraseButtonBank final : public juce::Component
{
public:
    PhraseButtonBank();
    ~PhraseButtonBank() override;

    PhraseButtonBank (const PhraseButtonBank&)            = delete;
    PhraseButtonBank& operator= (const PhraseButtonBank&) = delete;

    /// Push the eight SlotViews for the currently-visible bank. Fewer than 8
    /// entries pads the tail with inert empties. Repaints.
    void setSlots (std::vector<SlotView> slots);

    /// Current 0-based page + total page count → drives chevron enable state
    /// and the page indicator. MainComponent computes pages from phrase count.
    void setPaging (int currentPage, int totalPages);

    /// Begin the inline rename editor over button `positionInBank` (1..8).
    void beginRename (int positionInBank, const juce::String& currentName);

    // --- relays (MainComponent wires these) ---
    /// A button at 1-based bank position was tapped (trigger).
    std::function<void (int positionInBank)>                       onTrigger;
    /// Context gesture on a button → screen point for the menu anchor.
    std::function<void (int positionInBank, juce::Point<int>)>     onContextMenu;
    /// Rename committed for a button.
    std::function<void (int positionInBank, juce::String)>         onCommitName;
    /// Page left / right requested via a chevron.
    std::function<void (int delta)>                                onPage;   // -1 or +1

    void resized() override;

private:
    class SlotButton;   // defined in the .cpp — owns paint + gesture + editor
    std::vector<std::unique_ptr<SlotButton>> buttons_;        // exactly 8
    juce::TextButton prevChevron_ { "<" };
    juce::TextButton nextChevron_ { ">" };
    juce::Label      pageLabel_;                              // "1 / 3"
};

} // namespace ida
```

- [ ] **Step 3** Write `ui/src/PhraseButtonBank.cpp`. `SlotButton` carries one `SlotView`, paints `slotColour(view_)` as its fill with the label centred, and reuses the StripContextOverlay gesture logic (short-tap → `onTap`, right-click/long-press → `onMenu`, drag cancels long-press) plus the inline rename `TextEditor`. Use real code modeled on `StripContextOverlay`:

```cpp
#include "ida/PhraseButtonBank.h"

namespace ida
{

/// One slot. Holds its SlotView, paints state colour + label, and detects the
/// short-tap (trigger) vs right-click / 500 ms long-press (menu) gesture — the
/// same pattern as app/StripContextOverlay so desktop and iOS share one path.
class PhraseButtonBank::SlotButton final : public juce::Component, private juce::Timer
{
public:
    SlotButton (int position1Based) : position_ (position1Based)
    {
        setInterceptsMouseClicks (true, false);
    }

    void setView (SlotView v) { view_ = std::move (v); repaint(); }
    const SlotView& view() const noexcept { return view_; }

    void beginRename (const juce::String& current)
    {
        if (editor_ != nullptr) return;
        editor_ = std::make_unique<juce::TextEditor>();
        editor_->setText (current, false);
        editor_->selectAll();
        editor_->setColour (juce::TextEditor::backgroundColourId, otto::Colours::bg3);
        editor_->setColour (juce::TextEditor::textColourId,       otto::Colours::textPrimary);
        editor_->setColour (juce::TextEditor::outlineColourId,    otto::Colours::accent);
        editor_->onReturnKey = [this] { commit(); };
        editor_->onEscapeKey = [this] { cancel(); };
        editor_->onFocusLost = [this] { commit(); };
        addAndMakeVisible (*editor_);
        editor_->setBounds (getLocalBounds());
        editor_->grabKeyboardFocus();
        committed_ = false;
    }

    std::function<void (int)>                   onTap;
    std::function<void (int, juce::Point<int>)> onMenu;
    std::function<void (int, juce::String)>     onCommitName;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const auto fill = slotColour (view_);
        g.setColour (fill);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (otto::Colours::borderStrong);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        if (editor_ != nullptr) return;   // editor draws the text while renaming
        // Text colour: dark on bright state fills, light on the dim ones.
        const bool bright = view_.hasPhrase
                            && view_.state != PhraseLooperState::Stopped
                            && view_.state != PhraseLooperState::Empty;
        g.setColour (bright ? otto::Colours::textInverse : otto::Colours::textPrimary);
        g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        g.drawText (displayLabel(), getLocalBounds(), juce::Justification::centred, false);
    }

private:
    juce::String displayLabel() const
    {
        if (view_.label.isNotEmpty()) return view_.label;       // renamed
        return juce::String (position_);                        // default numeric
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (editor_ != nullptr) return;
        if (e.mods.isPopupMenu())                               // desktop right-click
        {
            if (onMenu) onMenu (position_, e.getScreenPosition());
            return;
        }
        armed_ = true;
        startTimer (500);                                       // iOS long-press
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isTimerRunning() && e.getDistanceFromDragStart() > 8) { stopTimer(); armed_ = false; }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool wasArmed   = armed_;
        const bool stillArmed = isTimerRunning();
        stopTimer();
        armed_ = false;
        if (wasArmed && stillArmed && ! e.mods.isPopupMenu() && onTap)
            onTap (position_);                                  // short tap = trigger
    }
    void timerCallback() override
    {
        stopTimer();
        if (armed_) { armed_ = false; if (onMenu) onMenu (position_, localToScreenCentre()); }
    }
    juce::Point<int> localToScreenCentre() const
    {
        return localPointToGlobal (getLocalBounds().getCentre());
    }
    void commit()
    {
        if (committed_ || editor_ == nullptr) return;
        committed_ = true;
        const auto txt = editor_->getText().trim();
        editor_.reset();
        if (onCommitName) onCommitName (position_, txt);       // empty == reset to numeric
    }
    void cancel() { committed_ = true; editor_.reset(); }

    int      position_ { 0 };
    SlotView view_;
    std::unique_ptr<juce::TextEditor> editor_;
    bool armed_     { false };
    bool committed_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotButton)
};

PhraseButtonBank::PhraseButtonBank()
{
    addAndMakeVisible (prevChevron_);
    addAndMakeVisible (nextChevron_);
    prevChevron_.onClick = [this] { if (onPage) onPage (-1); };
    nextChevron_.onClick = [this] { if (onPage) onPage (+1); };

    pageLabel_.setJustificationType (juce::Justification::centred);
    pageLabel_.setColour (juce::Label::textColourId, otto::Colours::textSecondary);
    addAndMakeVisible (pageLabel_);

    for (int p = 1; p <= kPhraseBankButtons; ++p)
    {
        auto btn = std::make_unique<SlotButton> (p);
        btn->onTap         = [this] (int pos)                    { if (onTrigger)     onTrigger (pos); };
        btn->onMenu        = [this] (int pos, juce::Point<int> s){ if (onContextMenu) onContextMenu (pos, s); };
        btn->onCommitName  = [this] (int pos, juce::String n)    { if (onCommitName)  onCommitName (pos, n); };
        addAndMakeVisible (*btn);
        buttons_.push_back (std::move (btn));
    }
}

PhraseButtonBank::~PhraseButtonBank() = default;

void PhraseButtonBank::setSlots (std::vector<SlotView> slots)
{
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setView (i < slots.size() ? slots[i] : SlotView {});
}

void PhraseButtonBank::setPaging (int currentPage, int totalPages)
{
    prevChevron_.setEnabled (currentPage > 0);
    nextChevron_.setEnabled (currentPage + 1 < totalPages);
    pageLabel_.setText (juce::String (currentPage + 1) + " / " + juce::String (totalPages),
                        juce::dontSendNotification);
}

void PhraseButtonBank::beginRename (int positionInBank, const juce::String& currentName)
{
    const int idx = positionInBank - 1;
    if (idx >= 0 && idx < static_cast<int> (buttons_.size()))
        buttons_[static_cast<std::size_t> (idx)]->beginRename (currentName);
}

void PhraseButtonBank::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    const int chevronW = 32;
    prevChevron_.setBounds (r.removeFromLeft (chevronW));
    nextChevron_.setBounds (r.removeFromRight (chevronW));
    pageLabel_.setBounds   (r.removeFromRight (56));
    r.removeFromLeft (4);
    r.removeFromRight (4);

    const int gap = 4;
    const int each = (r.getWidth() - gap * (kPhraseBankButtons - 1)) / kPhraseBankButtons;
    for (auto& b : buttons_)
    {
        b->setBounds (r.removeFromLeft (each));
        r.removeFromLeft (gap);
    }
}

} // namespace ida
```

- [ ] **Step 3a** Confirm `SlotView` is move-constructible (it holds a `juce::String` + PODs — yes) so `setView(std::move(...))` is valid.

- [ ] **Step 4** Build the UI library + tests (the bank must compile clean under `IdaUi`'s strict warnings):
`cmake --build build --target IdaUi IdaTests`
Expected: links green. Fix any warning-as-error (e.g. unused param) before proceeding.

- [ ] **Step 5** Commit the Component:
```bash
git add ui/include/ida/PhraseButtonBank.h ui/src/PhraseButtonBank.cpp ui/CMakeLists.txt
git commit -m "feat: PhraseButtonBank component — 8 buttons + chevrons, paging, tap/long-press gesture, inline rename"
```

---

### Task 6 — MainComponent: place the bank + build SlotViews from the phrase list

**Files:** Modify `app/MainComponent.h`, `app/MainComponent.cpp`.

> **HARD-STOP CHECK (do this first):** confirm Slice 5 has landed and exposes the per-phrase state + command seam (either the `IPhraseStateSource` / `IPhraseCommandSink` from Task 1, or concrete equivalents). Grep: `grep -rn "IPhraseStateSource\|IPhraseCommandSink\|dispatchPhraseTrigger\|looperStateFor\|PhraseLooperState" app/ core/include/ida/ | grep -v test`. If the only hits are this slice's own Task-1 headers and **no Slice 5 implementation exists**, you cannot wire a real trigger. Per `~/.claude/CLAUDE.md`, write a `## HALT` entry to `todo.md` (trigger: required asset missing — Slice 5 command/state impl), reply `<promise>HALTED</promise>`, and stop. Do NOT invent a parallel capture path (spec §8.4/§8.5 forbid it).

- [ ] **Step 1** In `app/MainComponent.h`, add the includes and members. Near the other `ui/include/ida` includes add `#include "ida/PhraseButtonBank.h"` and `#include "ida/PhraseMidiBinding.h"`. In the private members (next to `transportBarHost_`, ~line 516) add:
```cpp
    std::unique_ptr<ida::PhraseButtonBank> phraseBank_;
    int phraseBankPage_ { 0 };   // 0-based; which bank of 8 the row shows
    /// Per-phrase MIDI trigger bindings (stored now; live trigger is future).
    std::unordered_map<std::int64_t, ida::PhraseMidiBinding> phraseMidiBindings_;
    /// Copy/paste buffer for the phrase context menu (the source phrase id).
    std::optional<ida::ConstituentId> phraseCopyBuffer_;
```
And in the method declarations region (near `refreshOutputMixerPhraseChannels`):
```cpp
    /// Rebuild the phrase-button bank's eight SlotViews for the current page
    /// from the same phrase list refreshOutputMixerPhraseChannels uses, push
    /// state colour + labels, and update chevron/paging. Message-thread only.
    void refreshPhraseBank();
    /// A bank button at 1-based position was tapped — resolve to a phrase id
    /// and dispatch the source-agnostic trigger command (Slice 5). Inert when
    /// the slot has no phrase.
    void onPhraseButtonTrigger (int positionInBank);
    /// Build + show the single phrase-button context menu (Clear / Copy /
    /// Paste / Rename / Assign MIDI…). `screenPos` anchors mouse + touch.
    void showPhraseButtonMenu (int positionInBank, juce::Point<int> screenPos);
    /// Commit a button rename → set the phrase's display name (undoable).
    void onCommitPhraseButtonRename (int positionInBank, juce::String name);
    /// 0-based slot index for a 1-based bank position on the current page.
    int phraseSlotForPosition (int positionInBank) const;
```

- [ ] **Step 2** In `app/MainComponent.cpp` add the bank-height constant beside the transport one (~line 129):
```cpp
    constexpr int kPhraseBankHeightPx = 44;   // one row under the transport bar
```

- [ ] **Step 3** Construct the bank where `transportBarHost_` is created (~line 5929) and wire its relays:
```cpp
    phraseBank_ = std::make_unique<ida::PhraseButtonBank>();
    phraseBank_->onTrigger     = [this] (int pos)                     { onPhraseButtonTrigger (pos); };
    phraseBank_->onContextMenu = [this] (int pos, juce::Point<int> s) { showPhraseButtonMenu (pos, s); };
    phraseBank_->onCommitName  = [this] (int pos, juce::String n)     { onCommitPhraseButtonRename (pos, n); };
    phraseBank_->onPage        = [this] (int delta)
    {
        const int pages = ida::pageCount (currentPhraseCount());   // helper below
        phraseBankPage_ = juce::jlimit (0, juce::jmax (0, pages - 1), phraseBankPage_ + delta);
        refreshPhraseBank();
    };
    addAndMakeVisible (*phraseBank_);
```

- [ ] **Step 4** Dock the bank in `resized()` directly under the transport bar. Change the top region (currently `app/MainComponent.cpp:6131-6133`):
```cpp
    if (transportBarHost_ != nullptr)
        transportBarHost_->setBounds (area.removeFromTop (kTransportBarDesktopHeightPx));
    if (phraseBank_ != nullptr)
        phraseBank_->setBounds (area.removeFromTop (kPhraseBankHeightPx));
    tabs_.setBounds (area);
```

- [ ] **Step 5** Add a small private helper `currentPhraseCount()` and `phraseSlotForPosition()` near `refreshOutputMixerPhraseChannels`. Reuse the EXACT phrase enumeration the output-mixer refresh uses — the `selectTimelineView(...).pills` DFS order (positional + stable, spec §8.5):
```cpp
namespace
{
    std::vector<ida::ConstituentId> currentPhraseIds (MainComponent& mc); // fwd if needed
}

int MainComponent::phraseSlotForPosition (int positionInBank) const
{
    return ida::slotIndexFor (phraseBankPage_ + 1, positionInBank);   // page is 0-based
}
```
Implement phrase enumeration inline in `refreshPhraseBank` (Step 6) using the same selector call already present at line 7113; do not duplicate the selector wrapper — factor a tiny local lambda if the call is long.

- [ ] **Step 6** Implement `refreshPhraseBank()`:
```cpp
void MainComponent::refreshPhraseBank()
{
    if (phraseBank_ == nullptr) return;

    // Same phrase list, same order as refreshOutputMixerPhraseChannels (pills
    // are DFS order — positional + stable, spec §8.5).
    const auto timeline = selectTimelineView (*undoStack_.current(),
                                              demo_.sessionToLmc,
                                              inputs_,
                                              armedTapesVec(),
                                              focusedTape_);
    const auto& pills = timeline.pills;
    const int   total = ida::pageCount (static_cast<int> (pills.size()));
    phraseBankPage_ = juce::jlimit (0, juce::jmax (0, total - 1), phraseBankPage_);

    std::vector<ida::SlotView> slots;
    slots.reserve (ida::kPhraseBankButtons);
    for (int pos = 1; pos <= ida::kPhraseBankButtons; ++pos)
    {
        const int slot = ida::slotIndexFor (phraseBankPage_ + 1, pos);
        ida::SlotView v;   // default: inert empty
        if (slot >= 0 && slot < static_cast<int> (pills.size()))
        {
            v.hasPhrase = true;
            v.phraseId  = pills[static_cast<std::size_t> (slot)].id;
            v.state     = phraseLooperStateFor (v.phraseId);   // Slice 5 seam (Step 7)
            // Default label is the 1-based phrase NUMBER (absolute, not the
            // on-screen position) unless the phrase carries a custom name.
            const auto& nm = pills[static_cast<std::size_t> (slot)].name;
            v.label = nm.empty() ? juce::String (slot + 1) : juce::String (nm);
        }
        slots.push_back (std::move (v));
    }
    phraseBank_->setSlots (std::move (slots));
    phraseBank_->setPaging (phraseBankPage_, total);
}
```

- [ ] **Step 7** Add `phraseLooperStateFor(ConstituentId)` — the adapter to Slice 5. If Slice 5 exposes `IPhraseStateSource`, hold a pointer to it and forward. If Slice 5 exposes a concrete state object, map its combined-state id (0–6, spec §8) to the five buckets here:
```cpp
ida::PhraseLooperState MainComponent::phraseLooperStateFor (ida::ConstituentId phrase) const
{
    // Adapter to Slice 5. Replace the body with the real source once Slice 5
    // lands. Mapping (spec §8 ids → §8.5 colour states):
    //   rec states (4,5,6) -> Recording; loop-recording (3) -> Overdub;
    //   playing -> Playing; exists/idle (1,2) -> Stopped; absent -> Empty.
    if (phraseStateSource_ != nullptr)
        return phraseStateSource_->looperStateFor (phrase);
    return ida::PhraseLooperState::Stopped;   // safe default until Slice 5 binds
}
```
Add the member `ida::IPhraseStateSource* phraseStateSource_ { nullptr };` and a setter the Slice-5 wiring calls. (If Slice 5's concrete type differs, bind it here; keep the five-bucket output.)

- [ ] **Step 8** Call `refreshPhraseBank()` everywhere `refreshOutputMixerPhraseChannels()` is called (so the bank stays in lockstep with phrase create/delete/rename). Grep: `grep -n "refreshOutputMixerPhraseChannels()" app/MainComponent.cpp` and add a `refreshPhraseBank();` next to each call site (notably `refreshPreparation` / `refreshPerformance`).

- [ ] **Step 9** Build the app:
`cmake --build build --target IDA`
Expected: links green.

---

### Task 7 — The single context menu: Clear / Copy / Paste / Rename / Assign MIDI…

**Files:** Modify `app/MainComponent.cpp`.

All five ops live on ONE `juce::PopupMenu` (the IDA-side convention — `StripContextOverlay` and pane menus use `juce::PopupMenu`; the OTTO `TouchMenuPresenter` rule is OTTO-internal and does not apply to IDA). Right-click and long-press both reach this menu (Task 5 routes both to `onContextMenu`).

- [ ] **Step 1** Implement `showPhraseButtonMenu`. An empty slot shows only the inert state (no actionable items except Paste-into-empty if a copy buffer exists — but Paste creating a phrase is out of scope here, so an empty slot's menu is suppressed). Active slots show all five:
```cpp
void MainComponent::showPhraseButtonMenu (int positionInBank, juce::Point<int> screenPos)
{
    const int slot = phraseSlotForPosition (positionInBank);
    const auto timeline = selectTimelineView (*undoStack_.current(), demo_.sessionToLmc,
                                              inputs_, armedTapesVec(), focusedTape_);
    const auto& pills = timeline.pills;
    if (slot < 0 || slot >= static_cast<int> (pills.size()))
        return;   // inert empty slot — no menu (these buttons never CREATE, spec §8.5)

    const auto phraseId = pills[static_cast<std::size_t> (slot)].id;

    juce::PopupMenu menu;
    menu.addItem ("Clear", [this, phraseId] { clearPhrase (phraseId); });        // removes phrase, NEVER its tape (§2.1)
    menu.addSeparator();
    menu.addItem ("Copy",  [this, phraseId] { phraseCopyBuffer_ = phraseId; });
    menu.addItem ("Paste",
                  /*enabled*/ phraseCopyBuffer_.has_value(),
                  /*checked*/ false,
                  [this, phraseId] { pastePhraseOnto (phraseId); });
    menu.addSeparator();
    menu.addItem ("Rename\xe2\x80\xa6",
                  [this, positionInBank, slot, &pills]
                  {
                      const auto& nm = pills[static_cast<std::size_t> (slot)].name;
                      phraseBank_->beginRename (positionInBank,
                          nm.empty() ? juce::String (slot + 1) : juce::String (nm));
                  });
    menu.addSeparator();
    menu.addItem ("Assign MIDI\xe2\x80\xa6", [this, phraseId] { showAssignMidiDialog (phraseId); });

    menu.showMenuAsync (juce::PopupMenu::Options{}.withTargetScreenArea (
        juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)));
}
```
> Note: capturing `&pills` into the Rename lambda is unsafe (the local `timeline` dies when the method returns but `showMenuAsync` is async). FIX in implementation: capture the resolved `juce::String currentName` by value, not `&pills`. The plan flags this so the implementer captures the computed name string, e.g. compute `const juce::String currentName = nm.empty() ? juce::String(slot+1) : juce::String(nm);` before the `addItem` and capture `currentName`.

- [ ] **Step 2** Implement `clearPhrase(ConstituentId)` — remove the phrase Constituent from the tree (a derived view), push one labeled `UndoStack` entry ("Clear phrase"), and **never touch its tape** (spec §2.1). Reuse the existing tree-edit + `undoStack_` push pattern used by promotion at `app/MainComponent.cpp:8423`. After the edit call `refreshAll()` (or at least `refreshPhraseBank()` + `refreshOutputMixerPhraseChannels()`).

- [ ] **Step 3** Implement `pastePhraseOnto(ConstituentId target)` — a minimal, undoable duplication of the copied phrase's structure adjacent to / replacing the target. Scope: duplicate the phrase wrapper + its loop children referencing the SAME tape regions (a non-destructive copy — no tape data is copied; spec §2.1). Push one `UndoStack` entry ("Paste phrase"). If full structural paste is larger than this slice's budget, implement Copy + Paste of the phrase's *name + MIDI binding* only and record the structural-duplication remainder in `todo.md` with the exact file + next step (per the no-silent-deferral rule). **Decide and state which in the commit.**

- [ ] **Step 4** Implement `showAssignMidiDialog(ConstituentId)` — a small modal (`juce::AlertWindow` with note / channel / port fields, or a 3-field custom `juce::Component` in a `DialogWindow`) that reads the current `phraseMidiBindings_[phraseId.value()]`, lets the operator set note (0–127) / channel (1–16, 0=omni) / port (a `ComboBox` of `juce::MidiInput::getAvailableDevices()` names), and on OK stores the `ida::PhraseMidiBinding` back into the map. Push one `UndoStack` entry ("Assign MIDI") so it is undoable (spec §8.5: ops undoable). No live MIDI handling — storage only (future §14).

- [ ] **Step 5** Implement `onPhraseButtonTrigger(int positionInBank)`:
```cpp
void MainComponent::onPhraseButtonTrigger (int positionInBank)
{
    const int slot = phraseSlotForPosition (positionInBank);
    const auto timeline = selectTimelineView (*undoStack_.current(), demo_.sessionToLmc,
                                              inputs_, armedTapesVec(), focusedTape_);
    const auto& pills = timeline.pills;
    if (slot < 0 || slot >= static_cast<int> (pills.size())) return;   // inert
    ida::SlotView v; v.hasPhrase = true; v.phraseId = pills[static_cast<std::size_t> (slot)].id;
    if (phraseCommandSink_ != nullptr)
        ida::triggerSlot (v, *phraseCommandSink_);     // the ONE source-agnostic path (§8.4)
    refreshPhraseBank();                               // reflect the new state colour
}
```
Add the member `ida::IPhraseCommandSink* phraseCommandSink_ { nullptr };` + a setter the Slice-5 wiring calls. **If Slice 5's sink is absent, this is the HARD-STOP from Task 6 Step 0 — do not stub a fake command path.**

- [ ] **Step 6** Implement `onCommitPhraseButtonRename(int positionInBank, juce::String name)` — resolve the slot → phrase id, set the phrase's display name (empty name resets to the numeric default), push one `UndoStack` entry ("Rename phrase"), and `refreshPhraseBank()`. The label is cosmetic — positional mapping is untouched (spec §8.5).

- [ ] **Step 7** Build:
`cmake --build build --target IDA`
Expected: links green. Resolve the `&pills` capture flagged in Step 1 (capture the name string by value).

- [ ] **Step 8** Commit:
```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: dock phrase-button bank under transport, wire trigger + context menu (Clear/Copy/Paste/Rename/Assign MIDI)"
```

---

### Task 8 — Persist per-button MIDI bindings + bank page

**Files:** Modify the persistence path (`persistence/` `SessionFormat` source) + `app/MainComponent.cpp`.

The MIDI binding is stored "now" per spec §8.5 — it must survive save/load.

- [ ] **Step 1** Locate the session serialize/deserialize for per-phrase / per-constituent metadata (grep: `grep -rn "serializ\|deserializ" persistence/ | grep -i "phrase\|constituent\|metadata"`). Add a `phraseMidiBindings` map to the serialized session: key = constituent id, value = `{ note, channel, port }`. Round-trip via the existing JSON helpers (mirror an existing per-id map if one exists, e.g. how bus/strip state persists).

- [ ] **Step 2** Add a headless round-trip test for the binding map in the appropriate persistence test file (e.g. extend `tests/SessionFormatTests.cpp` or add a focused case):
```cpp
TEST_CASE ("phrase MIDI bindings round-trip through SessionFormat", "[sessionformat][phrase-bank]")
{
    // Build a session value with one phrase binding {note=60, channel=10, port="IAC Bus 1"};
    // serialize -> deserialize -> assert the binding survives by constituent id.
    // (Construct using the same fixtures the surrounding SessionFormat tests use.)
}
```
Fill in using the surrounding tests' fixture style.

- [ ] **Step 3** On load, populate `phraseMidiBindings_` from the deserialized session; on save, write `phraseMidiBindings_` out. Do NOT persist `phraseBankPage_` to the session file (it is transient view state) — leave it at 0 on load. State this choice in the commit.

- [ ] **Step 4** Build + run:
`cmake --build build --target IdaTests && ctest --test-dir build -R "sessionformat|phrase-bank"`
Expected: PASS.

- [ ] **Step 5** Commit:
```bash
git add persistence app/MainComponent.cpp tests
git commit -m "feat: persist per-phrase MIDI trigger bindings across session save/load"
```

---

### Task 9 — Clean build + operator verification (GUI)

**Files:** none (verification only).

- [ ] **Step 1** Clean build (mandatory before operator hand-off, per `[[feedback_clean_builds_only_for_testing]]`):
```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IdaTests && ctest --test-dir build
cmake --build build --target IDA
```
Expected: tests green (baseline 449/450 — the 1 non-pass is the separately-run `MainComponentPluginEditorTests` exe), app links.

- [ ] **Step 2** Launch the app (Claude is authorized to launch — `[[feedback_can_launch_app]]`):
```bash
open "build/app/IDA_artefacts/Release/IDA.app"
```

- [ ] **Step 3** Hand the operator these numbered checks (terse, one action each — `[[feedback_clean_builds_only_for_testing]]` / `[[feedback_short_responses]]`):
1. Confirm a single horizontal row `< 1 2 3 4 5 6 7 8 >` sits directly beneath the transport bar, above the tabs.
2. With no phrases yet, all 8 buttons read dim and numbered 1–8; tapping any does nothing.
3. Record a phrase (the §8 Record gesture). Button 1 lights with its state colour (red while recording, then green/dim per playback state).
4. Tap button 1 — it drives that phrase's state (play / stop / overdub) and the colour changes to match.
5. Right-click (desktop) / long-press (iOS) button 1 → the menu shows Clear, Copy, Paste, Rename, Assign MIDI….
6. Rename → type a name → Enter; the button shows the name; the phrase's positional slot is unchanged.
7. Clear → the button goes empty/dim; confirm via the Tapes tab that the tape still exists (Clear never deletes a tape).
8. Undo (the bottom-bar Undo) reverses Clear / Rename.
9. Assign MIDI… → set note/channel/port → OK; reopen the dialog and confirm the values persisted; save + reload the session and confirm they survive.
10. Create 9+ phrases; the `>` chevron enables; page to bank 2; confirm button 1 of bank 2 maps to phrase 9 (positional), and `<` returns to bank 1.

- [ ] **Step 4** Record the result. If all pass, the slice is done-when satisfied (spec §8.5). If any fail, debug per `superpowers:systematic-debugging` before re-handing.

---

## Self-review

- **Spec §8.5 coverage:** layout `< 1..8 >` under the transport bar → Task 6 Step 4 (docked after `transportBarHost_` in `resized()`); banks of 8 + chevron paging → Tasks 5 (`setPaging`, `onPage`) + 6 (`phraseBankPage_`, `pageCount`); positional mapping `(b-1)*8+p` → Task 2 (`slotIndexFor`, TDD'd) + 6 (used verbatim); empty slots inert → Tasks 3 (`slotColour` Empty → dim) + 4 (`triggerSlot` no-ops, TDD'd) + 7 (empty-slot menu suppressed); press → drive phrase state via Slice 5 command → Tasks 4 (`triggerSlot` → `IPhraseCommandSink`, equivalence TDD'd) + 7 Step 5; traditional state colours via `IdaPalette`/`OTTOColours` reconciled with identity hue → Task 3 (TDD'd, `phraseColour` for Stopped); ONE right-click/long-press menu (Clear/Copy/Paste/Rename/Assign MIDI) → Tasks 5 (gesture) + 7 (menu); Clear removes phrase not tape (§2.1) → Task 7 Step 2; ops undoable → Tasks 7 (UndoStack pushes) ; default numeric labels + Rename, label cosmetic → Tasks 5 (`displayLabel`) + 6 (label fill) + 7 Step 6; per-button MIDI note/channel/port stored → Tasks 1 (`PhraseMidiBinding`) + 7 Step 4 + 8 (persisted). ✓
- **"Bank is one more input source, not a parallel path" (§8.4):** enforced by routing every press through the single `triggerSlot` → `IPhraseCommandSink::dispatchPhraseTrigger`, and PROVEN by the Task 4 equivalence test (finger vs MIDI → identical command). No new capture logic. ✓
- **Headless vs operator split honoured:** all pure logic (bank math, state→colour, inertness, command equivalence, MIDI-binding round-trip) is Catch2 (Tasks 2–4, 8); the Component + placement + menu are operator-verified (Task 9). No fake GUI unit tests. ✓
- **Cross-slice risk surfaced:** the roadmap names Slice 5/6 as deps but not the API surface; this plan defines the `IPhraseStateSource`/`IPhraseCommandSink` seam in `core` (Task 1), keeps the bank + tests dependent only on the interface + a fake, and installs a HARD-STOP at Task 6/7 if Slice 5's real impl is absent (no parallel path). ✓
- **Reuse over reinvention:** the gesture + inline-rename logic is lifted from the proven `app/StripContextOverlay.h`; phrase enumeration reuses the exact `selectTimelineView(...).pills` call already at `app/MainComponent.cpp:7113`; colours come from the single palette source; menus use the IDA-side `juce::PopupMenu` convention (not OTTO's TouchMenuPresenter, which is OTTO-internal). ✓
- **Flagged implementation hazards:** the async `showMenuAsync` lambda must capture the rename name by value, not `&pills` (Task 7 Step 1 note); `phraseBankPage_` is transient and not persisted (Task 8 Step 3); Paste's structural-duplication scope is bounded with an explicit todo.md fallback if it overruns budget (Task 7 Step 3). ✓
- **RT-safety / stereo:** no audio-thread code is touched — the bank is message-thread UI reading state + dispatching commands that Slice 5 already routes safely; no mono audio introduced. ✓
- **Build hygiene:** new sources registered in `ui/CMakeLists.txt` + `tests/CMakeLists.txt`; clean `rm -rf build` before the operator hand-off (Task 9). Single-line commits with the required trailer. ✓
