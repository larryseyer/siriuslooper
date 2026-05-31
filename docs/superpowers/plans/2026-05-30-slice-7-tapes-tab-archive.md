# Slice 7 — Tapes Tab = Per-Input Archive + Reveal-in-Storage — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Turn the Tapes tab into the per-input **archive** of the project — never *required* to record, always *available* to *find* a complete take. Each project-owned tape lists by its `tape_<x>` index and the input that feeds it, shows a **recording indicator** (lit ⟺ ≥1 input is assigned to that tape — reusing Slice 4's predicate), and exposes a **Reveal-in-storage** button that opens the tape's on-disk file in the OS file browser (`juce::File::revealToUser()`), AUv3-extension-safe.

**Architecture:** The Tapes pane (`MainComponent::TapesPane`, `app/MainComponent.cpp:3664`) stays pure presentation + intent relay — it never touches `tapePool_`/`inputMixer_`. `MainComponent::refreshTapesPane()` (`app/MainComponent.cpp:8279`) computes, per pooled tape: (a) the feeding-input label, (b) the recording-on flag via the engine predicate `InputMixer::tapeHasAssignedInput(TapeId)` (Slice 4), and (c) the resolved on-disk `juce::File` via Slice 2's project-scoped path builder. The reveal action routes through an **extension-safe wrapper** so the standalone app reveals while an AUv3 extension no-ops. The recording-indicator predicate is the only headless-testable unit; the row list + reveal are operator-verified.

**Tech Stack:** C++/JUCE; `engine` (`InputMixer`, JUCE-free public API), `app` (`MainComponent`, `TapesPane`); Catch2 (`IdaTests`, `tests/InputMixerTests.cpp`); CMake + Ninja. Canonical design: `docs/superpowers/specs/2026-05-30-blank-slate-first-run-and-phrase-creation-design.md` §7.

**Dependencies:**
- **Slice 2** — `IdaProject` + project-scoped `tape_<x>` path builder (`juce::File tapeFile(const IdaProject&, TapeId)`). Reveal MUST open the file the *live writer* actually creates, so this slice resolves the file through Slice 2's builder, never a local copy of the naming rule.
- **Slice 4** — channels assigned to tapes + a "tape records iff ≥1 input assigned" predicate. This slice **consumes** that predicate; it does not reinvent it. If Slice 4 landed the predicate only as an app-side loop (the `removeTape` pattern at `app/MainComponent.cpp:8244`), Task 1 promotes it to the named engine method `InputMixer::tapeHasAssignedInput` so it is headless-testable and reusable.
- **Slice 1** — empty `TapePool` + `std::optional<TapeId> primary()`. The pane must render a legal zero-tape list and must NOT gate Remove on a `>=1` floor (that floor is gone).

---

## Cross-cutting constraints (apply to every task)

- **PARAMOUNT — never delete a tape here.** Reveal and indicator are read-only. This slice adds **no** tape-destroying path. The existing Remove button's destructive semantics are out of Slice 7's scope; Task 4 only stops it from *claiming* a `>=1` floor that no longer exists, it does not add or change deletion.
- **No orphans, all visible.** Every pooled tape gets exactly one row. A tape whose channel was removed still lists (recording indicator OFF) — it is archive, not an orphan (spec §2.2).
- **Extension-safety (`APPLICATION_EXTENSION_API_ONLY=YES`).** `juce::File::revealToUser()` reaches `[NSWorkspace sharedWorkspace]` / `[UIApplication sharedApplication]` internally. Route every reveal through one wrapper that compiles to a no-op when building an app-extension target. iOS reveal is Files-app-limited — the wrapper still calls `revealToUser()` on iOS standalone (best-effort), only the *extension* no-ops.
- **GUI = operator-verified.** Never fabricate a GUI unit test. The row list, the lit/dim indicator on screen, and "reveal opens the correct folder" are numbered operator steps. Only the predicate is Catch2.
- **Clean build before operator hand-off:** `rm -rf build` then reconfigure (`[[feedback_clean_builds_only_for_testing]]`).
- **Single-line commits**, trailer `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`. The implementing subagent pushes its task commits to `origin/master`.

---

## Task 1 — Headless predicate: `InputMixer::tapeHasAssignedInput` (TDD)

**Files:**
- Modify: `engine/include/ida/InputMixer.h` (declare next to `channelMainOutIsTape`, ~line 112)
- Modify: `engine/src/InputMixer.cpp` (implement next to `channelMainOutIsTape`, ~line 1057)
- Modify: `tests/InputMixerTests.cpp` (new cases)

**Rationale:** Slice 4's recording rule ("a tape records iff ≥1 input is assigned") is the indicator's truth source. Today the equivalent is an app-side loop over `inputStripChannelIds_` calling `channelMainOutIsTape` (`app/MainComponent.cpp:8244`). That cannot be unit-tested headless and re-derives Slice 4's rule in the UI layer. Promote it to a single engine method that walks the InputMixer's *own* registered channels — one source of truth for both Slice 4's recording-gate and Slice 7's indicator. If Slice 4 already added this exact method, skip the production edit and keep only the tests as regression coverage (verify by grep first).

- [ ] **Step 1: Confirm whether Slice 4 already added the predicate**

Run: `grep -rn "tapeHasAssignedInput\|tapeIsAssigned\|tapeHasInput\|isTapeAssigned" engine app`
- If a method with this contract exists, note its exact name; the rest of this plan uses `tapeHasAssignedInput` — if Slice 4 named it differently, alias the operator-verification + tests to that name and **skip Steps 3–4** (production edit), keeping Step 2 (tests) as regression.
- If absent, proceed with all steps.

- [ ] **Step 2: Write the failing tests in `tests/InputMixerTests.cpp`**

These encode *why* the indicator lights: a tape is "recording" exactly when at least one channel routes its main-out to that tape. Add near the existing tape-routing cases:

```cpp
TEST_CASE ("tapeHasAssignedInput is false for an unassigned / unknown tape", "[input-mixer][tape-assign]")
{
    ida::InputMixer mixer;
    REQUIRE (mixer.addTape (ida::TapeId (1)));
    REQUIRE (mixer.addTape (ida::TapeId (2)));
    // No channels yet -> nothing is assigned -> nothing records.
    CHECK_FALSE (mixer.tapeHasAssignedInput (ida::TapeId (1)));
    CHECK_FALSE (mixer.tapeHasAssignedInput (ida::TapeId (2)));
    // Unknown tape id is false, never a crash.
    CHECK_FALSE (mixer.tapeHasAssignedInput (ida::TapeId (999)));
}

TEST_CASE ("tapeHasAssignedInput lights only the tape a channel targets", "[input-mixer][tape-assign]")
{
    ida::InputMixer mixer;
    REQUIRE (mixer.addTape (ida::TapeId (1)));
    REQUIRE (mixer.addTape (ida::TapeId (2)));

    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.setChannelMainOutToTape (ch, ida::TapeId (2)));

    CHECK_FALSE (mixer.tapeHasAssignedInput (ida::TapeId (1)));
    CHECK      (mixer.tapeHasAssignedInput (ida::TapeId (2)));
}

TEST_CASE ("tapeHasAssignedInput counts multiple channels and clears on the last unassign (N:1 / archive)", "[input-mixer][tape-assign]")
{
    ida::InputMixer mixer;
    REQUIRE (mixer.addTape (ida::TapeId (1)));

    const auto a = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    const auto b = mixer.addChannel (ida::InputId (1), ida::SignalType::Audio);
    REQUIRE (mixer.setChannelMainOutToTape (a, ida::TapeId (1)));
    REQUIRE (mixer.setChannelMainOutToTape (b, ida::TapeId (1)));
    CHECK (mixer.tapeHasAssignedInput (ida::TapeId (1)));      // 2 inputs -> recording

    // Re-route one channel away; still one input left -> still recording.
    REQUIRE (mixer.setChannelMainOutToHardwareOutput (a));
    CHECK (mixer.tapeHasAssignedInput (ida::TapeId (1)));

    // Re-route the last channel away -> tape retained but stops recording (archive, spec §5).
    REQUIRE (mixer.setChannelMainOutToHardwareOutput (b));
    CHECK_FALSE (mixer.tapeHasAssignedInput (ida::TapeId (1)));
    CHECK (mixer.hasTape (ida::TapeId (1)));                   // retained, not removed
}
```

(Confirm `SignalType::Audio` and the `addChannel (InputId, SignalType)` signature against `engine/include/ida/InputMixer.h:220` before running; adjust enum/arg names if the live header differs.)

- [ ] **Step 3: Run the tests, verify they FAIL**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "tape-assign"`
Expected: FAIL — `tapeHasAssignedInput` does not exist (compile error) when Slice 4 didn't add it.

- [ ] **Step 4: Implement the predicate**

In `engine/include/ida/InputMixer.h`, declare beside `channelMainOutIsTape` (~line 112):

```cpp
    /// True iff at least one registered channel routes its main-out to `tape`
    /// (the spec §2/§5 "a tape records iff >=1 input is assigned" rule). False
    /// for an unassigned or unknown tape. This is the single source of truth for
    /// both the recording gate (Slice 4) and the Tapes-tab recording indicator
    /// (Slice 7); never re-derive it in the UI layer. Message-thread only.
    bool tapeHasAssignedInput (TapeId) const noexcept;
```

In `engine/src/InputMixer.cpp`, implement using the InputMixer's own channel registry (walk the same per-channel structures `channelMainOutIsTape` resolves through — e.g. the channel node table). Reuse `channelMainOutIsTape` per channel so the routing semantics stay identical:

```cpp
bool InputMixer::tapeHasAssignedInput (TapeId tape) const noexcept
{
    const MixerNodeId node = tapeNodeFor (tape);
    if (! node.isValid())
        return false;
    for (const auto& ch : channelNodes_)          // the existing per-channel registry
        if (graph_.mainOutOf (ch.node) == node)   // same test channelMainOutIsTape uses
            return true;
    return false;
}
```

Match the real member name for the channel registry — grep `channelNodes_\|channels_\|nodeForChannel` in `engine/src/InputMixer.cpp` and use whatever `channelMainOutIsTape`/`channelMainOut` already iterate or index. Stay `noexcept`, no allocation (message-thread accessor, but cheap and lock-free is free here).

- [ ] **Step 5: Run the tests, verify they PASS**

Run: `cmake --build build --target IdaTests && ctest --test-dir build -R "tape-assign"`
Expected: PASS. Then full engine regression: `ctest --test-dir build -R "input-mixer"` — green.

- [ ] **Step 6: Commit**

```bash
git add engine/include/ida/InputMixer.h engine/src/InputMixer.cpp tests/InputMixerTests.cpp
git commit -m "feat: InputMixer::tapeHasAssignedInput predicate for the Tapes-tab recording indicator"
```

---

## Task 2 — `TapeInfo` carries archive fields (label, recording, file) + Row renders indicator + Reveal

**Files:**
- Modify: `app/MainComponent.cpp` — `MainComponent::TapesPane::TapeInfo` (`:3668`), `TapesPane::Row` (`:3754`), `TapesPane::setTapes` (`:3697`)

**Behavior:** Each row shows, left-to-right: the `tape_<x>` index + feeding-input label (read-only), the editable name field (unchanged), a **recording indicator** (small lit/dim dot), a **Reveal** button, and the existing **Remove** button. The indicator and reveal-target are computed by `MainComponent` (Task 3) and pushed in via `TapeInfo` — the pane stays presentation-only.

- [ ] **Step 1: Extend `TapeInfo` with the archive fields**

Replace the struct at `app/MainComponent.cpp:3668`:

```cpp
    /// One pool entry's display state. The pane is presentation-only, so
    /// MainComponent::refreshTapesPane computes every field:
    ///   indexLabel  — "tape_<x>" (the on-disk file stem, spec §2.2)
    ///   inputLabel  — the input feeding this tape, or "" when unassigned (archive)
    ///   recording   — InputMixer::tapeHasAssignedInput(id) (Slice 4 predicate)
    ///   revealTarget— the file to reveal, falling back to the project folder
    ///                 when the writer has not flushed bytes yet (Task 3)
    struct TapeInfo
    {
        ida::TapeId  id;
        juce::String name;
        juce::String indexLabel;
        juce::String inputLabel;
        bool         recording { false };
        juce::File   revealTarget;
    };
```

- [ ] **Step 2: Add the reveal relay to `TapesPane`**

Beside `onCreate/onRename/onRemove` (~`app/MainComponent.cpp:3671`):

```cpp
    /// Reveal-in-storage intent. MainComponent wires this to the extension-safe
    /// reveal wrapper (Task 4) on the tape's resolved file. The pane never calls
    /// juce::File::revealToUser() itself (keeps the AUv3 guard in one place).
    std::function<void (ida::TapeId)> onReveal;
```

- [ ] **Step 3: Build the new fields into `Row` and render the indicator + Reveal**

Rewrite `Row` (`app/MainComponent.cpp:3754`) to accept the new fields and lay them out. Indicator is a tiny painted dot (no new asset) coloured through the palette: lit when recording, dim when not. Reveal is a `juce::TextButton`.

```cpp
    class Row final : public juce::Component
    {
    public:
        std::function<void (ida::TapeId, juce::String)> onRename;
        std::function<void (ida::TapeId)>               onRemove;
        std::function<void (ida::TapeId)>               onReveal;

        Row (const TapeInfo& info, bool canRemove) : id_ (info.id), recording_ (info.recording)
        {
            // Read-only "tape_<x>  <- Input N" descriptor; identity, not editable.
            index_.setText (info.inputLabel.isNotEmpty()
                                ? info.indexLabel + "   " + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90"))
                                      + " " + info.inputLabel
                                : info.indexLabel + "   (no input)",
                            juce::dontSendNotification);
            index_.setColour (juce::Label::textColourId,
                              info.inputLabel.isNotEmpty() ? otto::Colours::textPrimary
                                                           : otto::Colours::textSecondary);
            addAndMakeVisible (index_);

            name_.setText (info.name, juce::dontSendNotification);
            name_.setColour (juce::TextEditor::backgroundColourId, otto::Colours::bg3);
            name_.setColour (juce::TextEditor::textColourId, otto::Colours::textPrimary);
            name_.onReturnKey = [this] { commitName(); };
            name_.onFocusLost = [this] { commitName(); };
            addAndMakeVisible (name_);

            reveal_.setButtonText ("Reveal");
            reveal_.setTooltip (info.revealTarget.exists()
                                    ? "Show this tape's file in the file browser"
                                    : "Show this tape's project folder (no audio captured yet)");
            reveal_.onClick = [this] { if (onReveal) onReveal (id_); };
            addAndMakeVisible (reveal_);

            remove_.setButtonText ("Remove");
            remove_.setEnabled (canRemove);
            remove_.onClick = [this] { if (onRemove) onRemove (id_); };
            addAndMakeVisible (remove_);
        }

        void paint (juce::Graphics& g) override
        {
            // Recording indicator — a small dot in the universal looper language:
            // lit (record red via the palette) when >=1 input is assigned,
            // dim (textSecondary) when the tape is idle archive (spec §7).
            const auto dot = indicatorBounds_.toFloat();
            g.setColour (recording_ ? otto::Colours::error      // record/active red from the L&F
                                    : otto::Colours::textSecondary.withAlpha (0.4f));
            g.fillEllipse (dot);
        }

        void resized() override
        {
            constexpr int kGap = 6, kIndicator = 12;
            auto area = getLocalBounds();
            remove_.setBounds (area.removeFromRight (90));
            area.removeFromRight (kGap);
            reveal_.setBounds (area.removeFromRight (90));
            area.removeFromRight (kGap);
            auto dotCol = area.removeFromRight (kIndicator + kGap);
            indicatorBounds_ = dotCol.withSizeKeepingCentre (kIndicator, kIndicator);
            index_.setBounds (area.removeFromLeft (juce::jmin (220, area.getWidth() / 2)));
            area.removeFromLeft (kGap);
            name_.setBounds (area);
        }

    private:
        void commitName()
        {
            if (committed_) return;
            committed_ = true;
            const auto trimmed = name_.getText().trim();
            if (trimmed.isNotEmpty() && onRename) onRename (id_, trimmed);
        }

        ida::TapeId      id_;
        bool             recording_ { false };
        juce::Label      index_;
        juce::TextEditor name_;
        juce::TextButton reveal_;
        juce::TextButton remove_;
        juce::Rectangle<int> indicatorBounds_;
        bool             committed_ { false };
    };
```

(Confirm `otto::Colours::error` / `textSecondary` exist — they are already used elsewhere in this file at `:3727`, `:3686`. If the palette method per `docs/design/ida-colour-method.md` exposes a canonical "record/active" colour, prefer it over `error`; resolve which during implementation, never hardcode a hex.)

- [ ] **Step 4: Update `setTapes` to construct `Row` from the new `TapeInfo` and wire `onReveal`**

Rewrite `setTapes` (`app/MainComponent.cpp:3697`). The `>=1`-floor remove-gating is removed (Slice 1 made an empty pool legal); `canRemove` is left to whatever Task 4 settles — for this slice keep Remove enabled per existing primary rule only:

```cpp
    /// Rebuilds the row list from `infos` (one Row each), wiring rename/remove/
    /// reveal relays. The pane is presentation-only; MainComponent supplies the
    /// recording flag and reveal target. (The historical >=1 pool floor is gone
    /// — Slice 1 made an empty pool legal — so a single-tape pool no longer
    /// force-disables Remove here.)
    void setTapes (const std::vector<TapeInfo>& infos, std::optional<ida::TapeId> primary)
    {
        rows_.clear();
        for (const auto& info : infos)
        {
            const bool canRemove = ! (primary.has_value() && info.id == *primary);
            auto row = std::make_unique<Row> (info, canRemove);
            row->onRename = [this] (ida::TapeId id, juce::String n) { if (onRename) onRename (id, n); };
            row->onRemove = [this] (ida::TapeId id) { if (onRemove) onRemove (id); };
            row->onReveal = [this] (ida::TapeId id) { if (onReveal) onReveal (id); };
            addAndMakeVisible (*row);
            rows_.push_back (std::move (row));
        }
        resized();
    }
```

(`primary` becomes `std::optional<ida::TapeId>` to match Slice 1's `TapePool::primary()`. Add `#include <optional>` if the translation unit lacks it — it almost certainly already has it via the engine headers.)

- [ ] **Step 5: Bump row height for the wider row**

In `TapesPane::resized()` (`app/MainComponent.cpp:3730`) the `kRowH = 34` is fine for the added controls (Reveal + dot fit on one line); leave it unless the operator step in Task 5 reports clipping, then raise to `38`.

- [ ] **Step 6: Compile-check (no behavior verification yet)**

Run: `cmake --build build --target IDA`
Expected: links green. (Visual verification is Task 5, after Task 3 supplies real data.)

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: Tapes-tab rows show input label, recording indicator, and Reveal button"
```

---

## Task 3 — `refreshTapesPane()` computes label + recording flag + reveal target

**Files:**
- Modify: `app/MainComponent.cpp` — `refreshTapesPane()` (`:8279`)
- Read-only reference: Slice 2 path builder (`tapeFile(const IdaProject&, TapeId)`), `tapesDirectory()` (`:90`), the live writer filename (`audio/src/TapeRecordWriter.cpp:104`)

**Behavior:** For each pooled tape, fill `TapeInfo`: `indexLabel = "tape_<id>"`, `inputLabel` = the input feeding it (or empty), `recording = inputMixer_->tapeHasAssignedInput(id)`, `revealTarget` = Slice 2's resolved file if it exists else the project folder.

> **CROSS-SLICE RISK (file-path drift) — call out for the coordinator.** The *live writer* today writes `tapesDirectory()/tape-<id>.idatape` (`audio/src/TapeRecordWriter.cpp:104`) into the **flat** `…/IDA/tapes/` dir, and prefetch reads the same (`app/MainComponent.cpp:6059`). Slice 2's spec renames this to `…/IDA/<yyyymmddhhmmss-name>/tape_<x>` (FLAC). **Reveal MUST open the file the writer actually creates** — so the reveal target MUST come from the *same* path builder the `TapeRecordWriter` construction (`app/MainComponent.cpp:4263`) and prefetch use, not a Slice-7-local guess. The roadmap lists Slice 2 as a dependency but does **not** flag that the writer + prefetch call sites must already be migrated to the project-scoped builder for reveal to land on a real file. **If Slice 2 migrated only the path *helper* but left the writer/prefetch on the old flat `tape-<id>.idatape` name, reveal will point at an empty folder.** Task 3 Step 1 verifies this and, if the writer is still flat, resolves the reveal target from the *writer's* actual filename so it is correct regardless of which naming Slice 2 finished.

- [ ] **Step 1: Determine the authoritative reveal-path source**

Run: `grep -rn "getChildFile\|tape_\|tape-\|idatape\|\.flac\|tapeFile\|projectTapesDir" app/MainComponent.cpp audio/src/TapeRecordWriter.cpp | grep -iv test`
Decide the single function that yields a tape's on-disk file:
- **Preferred:** Slice 2 added a shared helper (e.g. `juce::File MainComponent::tapeFile (ida::TapeId) const` or a free `tapeFile(project, id)`) that **both** the `TapeRecordWriter` ctor and prefetch were migrated to. Use it.
- **Fallback (writer still flat):** mirror the writer's exact filename — `tapesDirectory().getChildFile ("tape-" + juce::String (id.value()) + ".idatape")` — so reveal matches reality. Add a one-line `// TODO(slice-2)` ONLY if the migration is genuinely incomplete, and in that case also append a `todo.md` entry per the no-silent-deferral rule (do not leave a bare TODO).

- [ ] **Step 2: Add a private helper to resolve the feeding-input label**

A tape's feeding input = the channel(s) whose main-out targets it. Reuse the existing `inputStripChannelIds_` + input-id parallel arrays (`inputStripInputIds_`, `app/MainComponent.h:530`) — the same arrays the strip context menu already maps. Add near `refreshTapesPane`:

```cpp
juce::String MainComponent::inputLabelForTape (ida::TapeId id) const
{
    // The input(s) feeding this tape, for the Tapes-tab archive row. Walks the
    // strip<->channel<->input parallel arrays (rebuildInputStrips keeps them in
    // lockstep). First match wins for the label; a "+N" suffix notes an N:1
    // submix tape (advanced routing, spec §6) without enumerating every source.
    juce::String first;
    int count = 0;
    for (std::size_t i = 0; i < inputStripChannelIds_.size(); ++i)
    {
        if (! inputMixer_->channelMainOutIsTape (inputStripChannelIds_[i], id))
            continue;
        ++count;
        if (first.isEmpty())
        {
            const auto iid = i < inputStripInputIds_.size() ? inputStripInputIds_[i]
                                                            : ida::InputId (0);
            first = inputDisplayName (iid);   // existing strip-name resolver; see Step 3
        }
    }
    if (count > 1) first << "  (+" << juce::String (count - 1) << ")";
    return first;
}
```

Declare it in `app/MainComponent.h` next to `refreshTapesPane();` (`:101`):
```cpp
    juce::String inputLabelForTape (ida::TapeId id) const;
```

- [ ] **Step 3: Reuse the existing input-name resolver (do not invent one)**

Run: `grep -rn "inputDisplayName\|displayNameForInput\|stripName\|labelForInput\|inputName" app/MainComponent.cpp`
- If a resolver from strip/channel rebuild exists, call it for `inputDisplayName(iid)`.
- If none exists, derive a minimal label inline from the device channel: for a hardware InputId, `"Input " + juce::String (iid.value() + 1)`; for a file-input InputId (`>= 100000`, per `app/MainComponent.h:526`), use the registry descriptor name already used by "Show player…" (`app/MainComponent.cpp:4886`). Keep it one helper; do not duplicate per call site.

- [ ] **Step 4: Rewrite `refreshTapesPane()` to fill the new fields and wire `onReveal`**

```cpp
void MainComponent::refreshTapesPane()
{
    if (tapesPane_ == nullptr) return;

    std::vector<TapesPane::TapeInfo> infos;
    infos.reserve (tapePool_.tapes().size());
    for (const auto& t : tapePool_.tapes())
    {
        TapesPane::TapeInfo info;
        info.id         = t.id;
        info.name       = juce::String (t.name);
        info.indexLabel = "tape_" + juce::String (t.id.value());     // on-disk stem, spec §2.2
        info.inputLabel = inputLabelForTape (t.id);
        info.recording  = inputMixer_ != nullptr
                              && inputMixer_->tapeHasAssignedInput (t.id);   // Slice 4 predicate
        const auto file = tapeFileFor (t.id);                        // Step 1's authoritative source
        info.revealTarget = file.existsAsFile() ? file : file.getParentDirectory();
        infos.push_back (std::move (info));
    }

    tapesPane_->setTapes (infos, tapePool_.primary());
}
```

Wire `onReveal` once where the other relays are set (`app/MainComponent.cpp:5941`):

```cpp
        tapesPane_->onReveal = [this] (ida::TapeId id)
        {
            const auto file = tapeFileFor (id);
            ida::revealInStorage (file.existsAsFile() ? file : file.getParentDirectory());
        };
```

(`tapeFileFor` is whatever Step 1 settled as the authoritative path source — either Slice 2's helper or the writer-matching fallback, exposed as a small private `MainComponent` method so refresh + the reveal relay share it. `ida::revealInStorage` is Task 4's wrapper.)

- [ ] **Step 5: Ensure the indicator stays live**

`refreshTapesPane()` is already called after pool mutations (`:8219`, `:8231`, `:8267`) and the assignment/channel edits land in Slice 4. Add a `refreshTapesPane();` call at the end of Slice 4's Add-Channel / channel-assignment handlers **iff** Slice 4 did not already (grep `refreshTapesPane` after this slice merges). The indicator does not need the 30 Hz timer — it only changes on assignment edits, which are message-thread events.

- [ ] **Step 6: Compile**

Run: `cmake --build build --target IDA`
Expected: green.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp app/MainComponent.h
git commit -m "feat: refreshTapesPane fills input label, recording flag, and reveal target per tape"
```

---

## Task 4 — Extension-safe reveal wrapper (`ida::revealInStorage`)

**Files:**
- Create: `app/RevealInStorage.h` (tiny header-only wrapper in the `ida` namespace)
- Modify: `app/CMakeLists.txt` only if a non-header file is added (header-only needs no source entry; confirm `app/` globs headers or that `MainComponent.cpp`'s include suffices)
- Modify: `app/MainComponent.cpp` (include it near the top with the other `ida/` includes)

**Rationale:** `juce::File::revealToUser()` reaches extension-forbidden APIs (`NSWorkspace`/`UIApplication`). The standalone `IDA` app must reveal; any future AUv3 extension target compiled with `APPLICATION_EXTENSION_API_ONLY=YES` must **not** call it (it would fail to link / be rejected). One wrapper centralises the guard so no other call site touches `revealToUser` directly (spec §7; `~/.claude/CLAUDE.md` AUv3-extension-safety rule).

- [ ] **Step 1: Write the wrapper**

```cpp
#pragma once

#include <juce_core/juce_core.h>

namespace ida
{
/// Reveal a file/folder in the OS file browser, AUv3-extension-safe.
///
/// juce::File::revealToUser() reaches [NSWorkspace sharedWorkspace] (macOS) and
/// [UIApplication sharedApplication] (iOS) internally — both forbidden when a
/// target is built with APPLICATION_EXTENSION_API_ONLY=YES. The standalone app
/// reveals; an app-extension build compiles this to a no-op so the AUv3 target
/// stays link-clean and submission-safe (project CLAUDE.md AUv3 rule, spec §7).
///
/// iOS note: even in the standalone app, reveal is limited to Files-app
/// integration — there is no Finder. We still issue the call (best-effort);
/// only the *extension* path no-ops.
inline void revealInStorage (const juce::File& target)
{
   #if defined (JucePlugin_Build_AUv3) && JucePlugin_Build_AUv3
    // Building inside the AUv3 extension: no shell, no NSWorkspace/UIApplication.
    juce::ignoreUnused (target);
   #else
    if (target != juce::File{} && target.exists())
        target.revealToUser();
   #endif
}
} // namespace ida
```

> Guard rationale: `JucePlugin_Build_AUv3` is JUCE's per-target macro, defined only when compiling the AUv3 extension. The repo has no AUv3 target *today* (`grep` for `JucePlugin_Build_AUv3` / `APPLICATION_EXTENSION_API_ONLY` returns nothing — confirmed during exploration), so on every current build this expands to the standalone branch and reveals normally. The guard is the forward-safety the spec mandates so the file is correct the moment an extension target is added. **Do not** gate on `JUCE_IOS` — iOS *standalone* should still attempt reveal; only the *extension* must no-op.

- [ ] **Step 2: Include it and confirm no other reveal call site**

Add `#include "RevealInStorage.h"` (or the path your `app/` include convention uses) near the other `ida/` includes at the top of `app/MainComponent.cpp`. Then:
Run: `grep -rn "revealToUser" app ui engine persistence core | grep -v RevealInStorage.h`
Expected: only the wrapper references `revealToUser`; the Task-3 `onReveal` relay calls `ida::revealInStorage`, never `revealToUser` directly.

- [ ] **Step 3: Compile**

Run: `cmake --build build --target IDA`
Expected: green (header-only; if `app/CMakeLists.txt` enumerates headers explicitly rather than globbing, add `RevealInStorage.h` to the target's source list).

- [ ] **Step 4: Commit**

```bash
git add app/RevealInStorage.h app/MainComponent.cpp app/CMakeLists.txt
git commit -m "feat: AUv3-extension-safe revealInStorage wrapper for Tapes-tab Reveal"
```

---

## Task 5 — Clean build + operator verification (GUI)

**Files:** none (verification only).

GUI behavior is operator-verified, never unit-tested. Build clean, launch, hand the operator the numbered steps below.

- [ ] **Step 1: Clean rebuild**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IDA
```
Expected: configures and links with no errors.

- [ ] **Step 2: Launch for the operator**

```bash
open "build/app/IDA_artefacts/Release/IDA.app"
```

- [ ] **Step 3: Operator verification — numbered steps (relay verbatim)**

1. Open the **Tapes** tab. With a brand-new (blank-slate) project and **no channels yet**, the list is **empty** (no rows, no crash).
2. In the **Input Mixer**, **Add a channel** and pick a physical input. Return to **Tapes**: exactly **one row** appears, reading **`tape_1  ← <your input>`**, and its **recording indicator dot is LIT** (record-red).
3. Click that row's **Reveal** button. The OS file browser opens showing the tape's file (or its project folder if no audio has been captured yet). Confirm the revealed location is **inside the project's folder** (`…/Application Support/IDA/<timestamp-name>/`), not the old flat `…/IDA/tapes/`.
4. **Remove that channel** in the Input Mixer. Back on **Tapes**: the row **remains** (archive — no orphan), the input label shows **`(no input)`**, and the indicator dot is now **DIM**. Reveal still works (opens the same file/folder — the tape was retained, not deleted).
5. Add **two** channels, then route both to the **same** tape via the advanced Tapes-tab/strip routing (if Slice 4/6 exposes it). The row shows the first input plus **`(+1)`** and the dot is **LIT**. (If N:1 routing UI is not yet exposed, skip — the engine already supports it per Task 1's tests.)
6. Confirm **no Remove click ever deletes audio without a warning** — Slice 7 added no destructive path; Remove behaves exactly as before this slice.

- [ ] **Step 4: Record the operator result**

On operator confirmation, the slice is done. If any step fails, fix surgically and re-run Task 5 from Step 1 (clean build before every hand-off).

---

## Self-review

- **Spec §7 coverage:** rows list each tape by `tape_<x>` index + feeding input (Tasks 2–3) ✓; recording indicator lit ⟺ ≥1 assigned input, **reusing** Slice 4's predicate promoted to `InputMixer::tapeHasAssignedInput` rather than reinvented (Task 1) ✓; Reveal-in-storage via `juce::File::revealToUser()` (Task 4) ✓; AUv3 extension-safety guard + iOS Files-app caveat documented in the wrapper (Task 4) ✓.
- **Archive invariants:** every pooled tape gets a row including channel-removed tapes (indicator OFF, label "(no input)") — no orphans, never required to record, always available to find (Tasks 2–3, operator step 4) ✓.
- **PARAMOUNT safety:** read-only slice — no tape deletion added; Remove semantics untouched; the `>=1` floor removal in Task 2 Step 4 only matches Slice 1's already-legal empty pool, it does not enable destroying audio (operator step 6) ✓.
- **Headless vs operator split:** only the predicate is Catch2 (`tests/InputMixerTests.cpp`, Task 1); the row list + reveal-opens-correct-folder are numbered operator steps (Task 5), no fabricated GUI test ✓.
- **Cross-slice risks surfaced:** (1) reveal must use the **same** path source as the live writer/prefetch — flagged loudly in Task 3, with a writer-matching fallback if Slice 2 migrated the helper but not the writer/prefetch call sites; (2) Task 1 guards against double-adding the predicate if Slice 4 already did; (3) `primary()` is now `std::optional` (Slice 1) — `setTapes`/`refreshTapesPane` updated to match ✓.
- **Build/commit discipline:** clean `rm -rf build` before the operator hand-off (Task 5); single-line commits with the required trailer; each task is independently committable ✓.
- **No placeholders, no dead code:** every step has real code or explicit operator actions; the only conditional `TODO(slice-2)` is gated on a genuine incomplete-dependency and paired with a `todo.md` entry per the no-silent-deferral rule ✓.
