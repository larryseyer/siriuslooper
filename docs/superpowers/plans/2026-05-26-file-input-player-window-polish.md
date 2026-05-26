# File-input player window polish — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the file-input player window from "a stock Mac document window" into "a floating IDA source": no native title bar, semi-transparent background with controls staying sharp, drag-anywhere-on-background to move, close via right-click menu or `Cmd-W`, and a persisted always-on-top toggle.

**Architecture:** Keep `juce::DocumentWindow` as the base class — all changes are surgical edits to `app/FileInputPlayerWindow.{h,cpp}`, the `FileInputDescriptor`, the `FileInputRegistry`, and the persistence layer. Repurpose the existing `windowOpacity` field to mean "background fill alpha; controls at 1.0" rather than the current "whole-window `setAlpha()` dim." Add one new persisted field (`bool alwaysOnTop`).

**Tech Stack:** JUCE 8 (`DocumentWindow`, `ComponentDragger`, `KeyPress`, `ComponentPeer::windowIsSemiTransparent`), Catch2 (`TEST_CASE` with `[file-input][...]` tags), CMake/Ninja, C++17.

**Spec:** `docs/superpowers/specs/2026-05-26-file-input-player-window-polish-design.md` (commit `be9e688`). Read it first — this plan presupposes the spec's six locked decisions.

**Task ordering rationale:** Each commit must leave the app in a shippable state. T1 + T2 + T3 land the new close + drag gestures **before** T4 drops the native title bar; without that order, T4 alone would leave the operator with no way to close or move the window between commits.

**Deviation from spec §4:** Spec says test files live at `audio/tests/`. Actual location is `/Users/larryseyer/IDA/tests/` (top-level). Plan uses the actual paths; spec will be footnote-corrected at the end.

**Baseline commands** (referenced throughout):

```bash
cmake --build build --target IdaTests     # build the test exe
cmake --build build --target IDA          # build the app
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j   # whole suite (baseline 773/773)
./build/tests/IdaTests "[file-input]"     # tagged subset (35 cases / ~2800 assertions baseline)
```

The 3 known transient flakes (#279 OOP-host / #350 stateBlobForSlot / #756 Bus-process-OOP) pass on `--rerun-failed` — don't chase them.

---

## Task 1: Cmd-W keyboard close

Goal: operator can dismiss the player with `Cmd-W` (macOS) / `Ctrl-W` (Win/Linux). Lands first so close-via-keyboard exists before the close button disappears with the title bar in Task 4.

**Files:**
- Modify: `app/FileInputPlayerWindow.h` — add `bool keyPressed (const juce::KeyPress&) override;`
- Modify: `app/FileInputPlayerWindow.cpp` — implement `keyPressed`; add `setWantsKeyboardFocus(true)` to ctor.

**Verification:** GUI; operator-verified. Build + a brief eyes-on confirmation.

- [ ] **Step 1: Add `setWantsKeyboardFocus(true)` to ctor**

In `app/FileInputPlayerWindow.cpp`, in the `FileInputPlayerWindow` ctor body, add this line right after the existing `setUsingNativeTitleBar (true);`:

```cpp
    setWantsKeyboardFocus (true);
```

- [ ] **Step 2: Declare `keyPressed` override in the header**

In `app/FileInputPlayerWindow.h`, inside the `public:` section of `FileInputPlayerWindow`, add after `void mouseDown (const juce::MouseEvent&) override;`:

```cpp
    bool keyPressed (const juce::KeyPress&) override;
```

- [ ] **Step 3: Implement `keyPressed` in the cpp**

In `app/FileInputPlayerWindow.cpp`, add after `void FileInputPlayerWindow::mouseDown (...)`:

```cpp
bool FileInputPlayerWindow::keyPressed (const juce::KeyPress& key)
{
    // Cmd-W on macOS, Ctrl-W on Win/Linux — JUCE's commandModifier resolves
    // to the platform-correct modifier so one handler covers both.
    if (key == juce::KeyPress ('w', juce::ModifierKeys::commandModifier, 0))
    {
        closeButtonPressed();
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}
```

- [ ] **Step 4: Build**

```bash
cmake --build build --target IDA
```

Expected: clean build, no warnings on the changed file.

- [ ] **Step 5: Commit**

```bash
git add app/FileInputPlayerWindow.h app/FileInputPlayerWindow.cpp
git commit -m "feat: app — file-input player window honours Cmd-W / Ctrl-W close"
git push origin master
```

---

## Task 2: Right-click "Close window" menu item

Goal: operator can dismiss the player from the existing right-click context menu (which today only shows the opacity submenu).

**Files:**
- Modify: `app/FileInputPlayerWindow.cpp` — extend `showOpacityMenu()` to prepend "Close window" + separator above the opacity submenu. (The method name stays for now — it's still the same menu; we're just adding to it. Renaming is out of scope.)

**Verification:** GUI; operator-verified. Build only.

- [ ] **Step 1: Add "Close window" item to the root menu**

In `app/FileInputPlayerWindow.cpp`, locate `void FileInputPlayerWindow::showOpacityMenu()`. The current end of the function is:

```cpp
    juce::PopupMenu root;
    root.addSubMenu ("Window opacity", opacity);
    root.showMenuAsync (juce::PopupMenu::Options {}
                            .withTargetComponent (this)
                            .withMousePosition());
```

Replace the `juce::PopupMenu root;` section so it reads:

```cpp
    juce::PopupMenu root;
    root.addItem ("Close window", [this] { closeButtonPressed(); });
    root.addSeparator();
    root.addSubMenu ("Window opacity", opacity);
    root.showMenuAsync (juce::PopupMenu::Options {}
                            .withTargetComponent (this)
                            .withMousePosition());
```

- [ ] **Step 2: Build**

```bash
cmake --build build --target IDA
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add app/FileInputPlayerWindow.cpp
git commit -m "feat: app — file-input player right-click menu gains 'Close window'"
git push origin master
```

---

## Task 3: Drag-to-move via `juce::ComponentDragger` on Content

Goal: clicking anywhere on the Content background (non-control area) drags the parent window. Lands before Task 4 drops the title bar so drag-via-Content exists in parallel with drag-via-title-bar.

**Files:**
- Modify: `app/FileInputPlayerWindow.cpp` — add a `juce::ComponentDragger dragger_;` member to `Content`; override `Content::mouseDown` and `Content::mouseDrag`.

**Verification:** GUI; operator-verified. Build + brief eyes-on (click and drag the window body — should move the window).

- [ ] **Step 1: Add `ComponentDragger` member to `Content`**

In `app/FileInputPlayerWindow.cpp`, in `class FileInputPlayerWindow::Content`, in the `private:` member block near the bottom (after `int lastListSize_ { -1 };` and before `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)`), add:

```cpp
    juce::ComponentDragger dragger_;
```

- [ ] **Step 2: Override `Content::mouseDown` to start a drag**

In the same `Content` class, add these two methods to the `public:` section after `selectedRowsChanged (int /*lastRowSelected*/)`:

```cpp
    // ------------------------------------------------------------------
    // Drag-to-move — clicking on Content background drags the parent
    // window. Child controls (buttons/scrubber/list) consume their own
    // mouseDown first, so only background clicks reach here.
    // ------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;   // right-click is handled by the parent window's mouseDown
        if (auto* top = getTopLevelComponent())
            dragger_.startDraggingComponent (top, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (auto* top = getTopLevelComponent())
            dragger_.dragComponent (top, e, nullptr);
    }
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target IDA
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add app/FileInputPlayerWindow.cpp
git commit -m "feat: app — file-input player window drags via ComponentDragger on Content"
git push origin master
```

---

## Task 4: Drop native title bar + semi-transparent background + repurpose `windowOpacity` to bg-only

Goal: the chrome change. Window goes frameless; background paints with `windowOpacity` alpha; controls stay at 1.0. Previous tasks already gave the operator drag-to-move (T3), Cmd-W (T1), and a Close menu item (T2), so dropping the title bar leaves the window fully usable.

**Files:**
- Modify: `app/FileInputPlayerWindow.h` — add `void paint (juce::Graphics&) override;` to `Content` (it's currently only declared inside the cpp; we declare the override there too).
- Modify: `app/FileInputPlayerWindow.cpp` — ctor chrome flags; `Content::paint` override; remove all `setAlpha` calls in the opacity menu paths; replace with `content_->repaint()`.

**Verification:** GUI; operator-verified. Build + operator eyes-on: window has no title bar, background is semi-transparent at 0.92 default, buttons/labels stay sharp.

- [ ] **Step 1: Add chrome flags + `setOpaque(false)` + semi-transparent peer in ctor**

In `app/FileInputPlayerWindow.cpp`, in the `FileInputPlayerWindow` ctor body, replace this section:

```cpp
    setUsingNativeTitleBar (true);
    setWantsKeyboardFocus (true);

    content_ = std::make_unique<Content> (registry_, id_);
    setContentNonOwned (content_.get(), true);

    setResizable (true, true);
    setResizeLimits (380, 220, 1200, 900);
    centreWithSize (getWidth(), getHeight());

    // Apply persisted opacity once on construction.
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        setAlpha (juce::jlimit (0.5f, 1.0f, d->windowOpacity));

    startTimerHz (30);
    setVisible (true);
```

with:

```cpp
    setUsingNativeTitleBar (false);
    setTitleBarHeight (0);
    setWantsKeyboardFocus (true);
    setOpaque (false);

    content_ = std::make_unique<Content> (registry_, id_);
    setContentNonOwned (content_.get(), true);

    setResizable (true, true);
    setResizeLimits (380, 220, 1200, 900);
    centreWithSize (getWidth(), getHeight());

    // Semi-transparent peer so Content::paint's alpha-fill renders correctly;
    // drop shadow keeps the floating window visually anchored. Must be set
    // before setVisible(true) so the peer is created with the right flags.
    addToDesktop (juce::ComponentPeer::windowHasDropShadow
                  | juce::ComponentPeer::windowIsSemiTransparent);

    startTimerHz (30);
    setVisible (true);
```

(The `setAlpha` line is gone — opacity is now applied per-paint by Content; no setAlpha anywhere.)

- [ ] **Step 2: Declare `Content::paint` override**

In `app/FileInputPlayerWindow.cpp`, in `class FileInputPlayerWindow::Content`, in the `public:` section near `void resized() override`, add:

```cpp
    void paint (juce::Graphics& g) override
    {
        const auto* d = registry_.fileInputDescriptor (id_);
        const float alpha = (d != nullptr)
            ? juce::jlimit (0.5f, 1.0f, d->windowOpacity)
            : 0.92f;
        const auto bg = getLookAndFeel().findColour (
            juce::ResizableWindow::backgroundColourId);
        g.fillAll (bg.withAlpha (alpha));
    }
```

- [ ] **Step 3: Remove `setAlpha` calls from opacity menu paths**

In `app/FileInputPlayerWindow.cpp`, in `void FileInputPlayerWindow::showOpacityMenu()`, replace the preset-item lambda body. Current code:

```cpp
        opacity.addItem (juce::String (pct) + "%",
                         true, ticked,
                         [this, pct]
                         {
                             const float a = pct / 100.0f;
                             registry_.setFileInputWindowOpacity (id_, a);
                             setAlpha (a);
                         });
```

becomes:

```cpp
        opacity.addItem (juce::String (pct) + "%",
                         true, ticked,
                         [this, pct]
                         {
                             const float a = pct / 100.0f;
                             registry_.setFileInputWindowOpacity (id_, a);
                             if (content_ != nullptr) content_->repaint();
                         });
```

In `void FileInputPlayerWindow::showCustomOpacityDialog()`, replace the slider's live-preview lambda. Current:

```cpp
    slider->onValueChange = [this, sliderPtr]
    {
        setAlpha (static_cast<float> (sliderPtr->getValue()));
    };
```

becomes (live-preview now writes a "preview" alpha by updating the descriptor through the registry, then repainting — keeps controls sharp during drag):

```cpp
    slider->onValueChange = [this, sliderPtr]
    {
        const float a = juce::jlimit (0.5f, 1.0f,
                                      static_cast<float> (sliderPtr->getValue()));
        registry_.setFileInputWindowOpacity (id_, a);
        if (content_ != nullptr) content_->repaint();
    };
```

Then in the modal callback at the end of `showCustomOpacityDialog`, the OK branch currently calls `setAlpha`:

```cpp
                if (result == 1)
                {
                    const float a = juce::jlimit (0.5f, 1.0f,
                                                  static_cast<float> (sliderOwned->getValue()));
                    registry_.setFileInputWindowOpacity (id_, a);
                    setAlpha (a);
                }
                else
                {
                    // Revert the live-preview alpha on cancel.
                    setAlpha (originalOpacity);
                }
```

becomes:

```cpp
                if (result == 1)
                {
                    const float a = juce::jlimit (0.5f, 1.0f,
                                                  static_cast<float> (sliderOwned->getValue()));
                    registry_.setFileInputWindowOpacity (id_, a);
                    // No setAlpha — Content::paint already reads the descriptor.
                }
                else
                {
                    // Cancel: restore the pre-dialog value through the registry
                    // so Content::paint reverts on the next repaint.
                    registry_.setFileInputWindowOpacity (id_, originalOpacity);
                }
                if (content_ != nullptr) content_->repaint();
```

- [ ] **Step 4: Build**

```bash
cmake --build build --target IDA
```

Expected: clean build, no warnings on the changed file.

- [ ] **Step 5: Verify the full test suite still passes**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j
```

Expected: 773 / 773 (allow the 3 known flakes to retry once via `--rerun-failed`). This task touches GUI code only; tests should be unaffected, but a regression sweep is cheap.

- [ ] **Step 6: Commit**

```bash
git add app/FileInputPlayerWindow.h app/FileInputPlayerWindow.cpp
git commit -m "feat: app — file-input player window is frameless with bg-only windowOpacity (controls stay sharp)"
git push origin master
```

**At this point the first half of the spec (chrome + drag + close gestures + opacity-repurpose) is complete.** The operator should eyes-on at this commit before moving on to Task 5 — if the chrome change reveals a JUCE quirk on macOS (semi-transparent peer not honouring the flag, control rendering misbehaviour, etc.), it's caught while the diff is small.

---

## Task 5: Add `bool alwaysOnTop` field to `FileInputDescriptor`

Goal: data-type addition. Pure header change, no behaviour change yet.

**Files:**
- Modify: `core/include/ida/FileInputDescriptor.h` — add `bool alwaysOnTop { false };` to the struct.

**Verification:** Build the test exe. The struct field is consumed by the registry/persistence later; this commit is the type-only change.

- [ ] **Step 1: Add the field**

In `core/include/ida/FileInputDescriptor.h`, in the `FileInputDescriptor` struct, add after `float windowOpacity { 0.92f };`:

```cpp
    bool alwaysOnTop { false };
```

Also update the struct-level doc comment (currently lines 30-38) to mention the field. Append to the existing comment, right before the `struct FileInputDescriptor` line:

```cpp
/// `alwaysOnTop` pins the player window above other windows (operator
/// preference; persisted; default false).
```

- [ ] **Step 2: Build**

```bash
cmake --build build --target IdaTests
```

Expected: clean build (the field is unused so far, default-initialized everywhere `FileInputDescriptor` is constructed).

- [ ] **Step 3: Commit**

```bash
git add core/include/ida/FileInputDescriptor.h
git commit -m "feat: core — FileInputDescriptor gains persisted alwaysOnTop field"
git push origin master
```

---

## Task 6: `FileInputRegistry::setFileInputAlwaysOnTop` setter + TDD test

Goal: registry method to flip the descriptor field. TDD: write the failing test first.

**Files:**
- Modify: `tests/FileInputRegistryTests.cpp` — add one new `TEST_CASE`.
- Modify: `audio/include/ida/FileInputRegistry.h` — declare `setFileInputAlwaysOnTop`.
- Modify: `audio/src/FileInputRegistry.cpp` — implement.

**Verification:** Catch2 test passes; full suite still 773 + 1 new.

- [ ] **Step 1: Write the failing test**

In `tests/FileInputRegistryTests.cpp`, add after the `setFileInputWindowOpacity` test case (around line 50):

```cpp
TEST_CASE ("FileInputRegistry::setFileInputAlwaysOnTop flips the descriptor field",
           "[file-input][registry]")
{
    ida::FileInputRegistry registry { 48000.0 };
    const auto id = registry.registerFileInput ({});

    REQUIRE (registry.fileInputDescriptor (id) != nullptr);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == false);

    registry.setFileInputAlwaysOnTop (id, true);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == true);

    registry.setFileInputAlwaysOnTop (id, false);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == false);

    // Unknown id is a no-op (does not crash, does not mutate other entries).
    registry.setFileInputAlwaysOnTop (ida::InputId (999999), true);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == false);
}
```

- [ ] **Step 2: Run the test to confirm it fails to compile**

```bash
cmake --build build --target IdaTests 2>&1 | tail -20
```

Expected: compile error along the lines of `'setFileInputAlwaysOnTop' is not a member of 'ida::FileInputRegistry'`.

- [ ] **Step 3: Declare the setter in the header**

In `audio/include/ida/FileInputRegistry.h`, add right after the `setFileInputWindowOpacity` declaration (around line 77):

```cpp
    /// Pins the player window above other windows. Pure descriptor mutation
    /// (windowing is message-thread; FileInputSource is unaware of this
    /// flag). No-op if `id` is unknown.
    void setFileInputAlwaysOnTop (InputId id, bool onTop);
```

- [ ] **Step 4: Implement the setter in the cpp**

In `audio/src/FileInputRegistry.cpp`, find the `setFileInputWindowOpacity` implementation and add directly after it:

```cpp
void FileInputRegistry::setFileInputAlwaysOnTop (InputId id, bool onTop)
{
    if (auto it = descriptors_.find (id.value()); it != descriptors_.end())
        it->second.alwaysOnTop = onTop;
}
```

(Match the exact style of `setFileInputWindowOpacity` — `descriptors_.find` + iterator check + direct field write.)

- [ ] **Step 5: Build and run the test**

```bash
cmake --build build --target IdaTests
./build/tests/IdaTests "[file-input][registry]"
```

Expected: the new test passes; the existing `[file-input][registry]` tests still pass.

- [ ] **Step 6: Run the full file-input subset to catch any cross-test interaction**

```bash
./build/tests/IdaTests "[file-input]"
```

Expected: 35 + 1 = 36 cases / ~2800+ assertions, all pass.

- [ ] **Step 7: Commit**

```bash
git add tests/FileInputRegistryTests.cpp audio/include/ida/FileInputRegistry.h audio/src/FileInputRegistry.cpp
git commit -m "feat: audio — FileInputRegistry::setFileInputAlwaysOnTop + unit test"
git push origin master
```

---

## Task 7: Persist `alwaysOnTop` (write/read) + TDD tests

Goal: round-trip the field through the session JSON. TDD; missing-key defaults to false.

**Files:**
- Modify: `tests/FileInputPersistenceTests.cpp` — add two new test cases.
- Modify: `audio/src/FileInputPersistence.cpp` — extend serialize/deserialize.
- Modify: `audio/include/ida/FileInputPersistence.h` — update the doc comment.

**Verification:** Both new Catch2 tests pass.

- [ ] **Step 1: Write the failing round-trip test**

In `tests/FileInputPersistenceTests.cpp`, add after the existing `windowOpacity` round-trip case (after line 40):

```cpp
TEST_CASE ("Session JSON round-trips alwaysOnTop=true",
           "[file-input][persistence]")
{
    ida::FileInputRegistry registry { 48000.0 };

    ida::FileInputDescriptor desc;
    desc.displayName  = "Pinned";
    desc.alwaysOnTop  = true;
    const auto id = registry.registerFileInput (desc);
    registry.addFileInputEntry (id, "/abs/x.wav");

    const auto json = ida::serializeFileInputs (registry);

    ida::FileInputRegistry registry2 { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry2, json));

    const ida::FileInputDescriptor* found = nullptr;
    for (const auto& [k, d] : registry2.allFileInputDescriptors())
        if (d.displayName == "Pinned") { found = &d; break; }

    REQUIRE (found != nullptr);
    CHECK (found->alwaysOnTop == true);
}
```

- [ ] **Step 2: Write the failing "missing key defaults to false" test**

In the same file, add directly after the test you just wrote:

```cpp
TEST_CASE ("Missing alwaysOnTop key in JSON defaults to false",
           "[file-input][persistence]")
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> fileInputs;
    juce::DynamicObject::Ptr fi = new juce::DynamicObject();
    fi->setProperty ("displayName",   "Legacy");
    fi->setProperty ("loopScope",     "off");
    fi->setProperty ("windowOpacity", 0.92);
    // alwaysOnTop intentionally NOT set — simulates a session JSON saved
    // before this slice landed.
    fi->setProperty ("entries", juce::Array<juce::var> {});
    fileInputs.add (juce::var (fi.get()));
    root->setProperty ("fileInputs", fileInputs);

    ida::FileInputRegistry registry { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry, juce::var (root.get())));

    REQUIRE (registry.allFileInputDescriptors().size() == 1u);
    CHECK (registry.allFileInputDescriptors().begin()->second.alwaysOnTop
           == false);
}
```

- [ ] **Step 3: Run the tests to confirm they fail**

```bash
cmake --build build --target IdaTests
./build/tests/IdaTests "[file-input][persistence]"
```

Expected: the round-trip test FAILS (because `serializeFileInputs` doesn't write `alwaysOnTop`, and `deserializeFileInputs` doesn't read it, so it stays false after round-trip even though we set it to true). The missing-key test may pass accidentally (false is the C++ default) but we want it for forward-compat regression coverage.

- [ ] **Step 4: Write `alwaysOnTop` in `serializeFileInputs`**

In `audio/src/FileInputPersistence.cpp`, in `serializeFileInputs`, find the line:

```cpp
        o->setProperty ("windowOpacity", d.windowOpacity);
```

Add directly after it:

```cpp
        o->setProperty ("alwaysOnTop",   d.alwaysOnTop);
```

- [ ] **Step 5: Read `alwaysOnTop` in `deserializeFileInputs`**

In the same file, in `deserializeFileInputs`, find the existing block:

```cpp
        const float opacity = (float) (double) f.getProperty ("windowOpacity", 0.92);
        desc.windowOpacity = std::clamp (opacity, 0.5f, 1.0f);
```

Add directly after it (BEFORE the `registerFileInput` call so the value lands in the descriptor passed to the registry):

```cpp
        desc.alwaysOnTop = (bool) f.getProperty ("alwaysOnTop", false);
```

- [ ] **Step 6: Update the persistence header's doc comment**

In `audio/include/ida/FileInputPersistence.h`, find the comment that lists the persisted subset (around line 11). It currently reads:

```cpp
/// persisted subset (displayName, loopScope, windowOpacity, entry paths);
```

Change it to:

```cpp
/// persisted subset (displayName, loopScope, windowOpacity, alwaysOnTop,
/// entry paths);
```

- [ ] **Step 7: Build and run the tests**

```bash
cmake --build build --target IdaTests
./build/tests/IdaTests "[file-input][persistence]"
```

Expected: both new tests pass; all existing `[file-input][persistence]` tests still pass.

- [ ] **Step 8: Run the full file-input subset**

```bash
./build/tests/IdaTests "[file-input]"
```

Expected: 36 + 2 = 38 cases pass.

- [ ] **Step 9: Commit**

```bash
git add tests/FileInputPersistenceTests.cpp audio/src/FileInputPersistence.cpp audio/include/ida/FileInputPersistence.h
git commit -m "feat: persistence — FileInputDescriptor.alwaysOnTop round-trips through session JSON"
git push origin master
```

---

## Task 8: Player window applies `alwaysOnTop` + menu toggle

Goal: the GUI half of always-on-top. Window ctor reads the descriptor on construction; right-click menu gains an "Always on top" toggle item.

**Files:**
- Modify: `app/FileInputPlayerWindow.cpp` — apply `setAlwaysOnTop` in ctor; add toggle item to the root context menu.

**Verification:** GUI; operator-verified. Build + operator eyes-on: toggle the menu item, window pins to top; restart the app, the pin survives.

- [ ] **Step 1: Apply persisted `alwaysOnTop` in ctor**

In `app/FileInputPlayerWindow.cpp`, in the `FileInputPlayerWindow` ctor body, find this section (added in Task 4):

```cpp
    addToDesktop (juce::ComponentPeer::windowHasDropShadow
                  | juce::ComponentPeer::windowIsSemiTransparent);

    startTimerHz (30);
    setVisible (true);
```

Insert directly before `startTimerHz (30);`:

```cpp
    // Apply persisted always-on-top flag once on construction. Subsequent
    // toggles come through the right-click menu (Task 8 step 2).
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        setAlwaysOnTop (d->alwaysOnTop);
```

- [ ] **Step 2: Add "Always on top" toggle item to the root menu**

In `app/FileInputPlayerWindow.cpp`, in `void FileInputPlayerWindow::showOpacityMenu()`, find the section (added in Task 2):

```cpp
    juce::PopupMenu root;
    root.addItem ("Close window", [this] { closeButtonPressed(); });
    root.addSeparator();
    root.addSubMenu ("Window opacity", opacity);
```

Replace with:

```cpp
    bool currentOnTop = false;
    if (const auto* d = registry_.fileInputDescriptor (id_); d != nullptr)
        currentOnTop = d->alwaysOnTop;

    juce::PopupMenu root;
    root.addItem ("Close window", [this] { closeButtonPressed(); });
    root.addSeparator();
    root.addItem ("Always on top",
                  true,                     // enabled
                  currentOnTop,             // ticked
                  [this, currentOnTop]
                  {
                      const bool next = ! currentOnTop;
                      registry_.setFileInputAlwaysOnTop (id_, next);
                      setAlwaysOnTop (next);
                  });
    root.addSubMenu ("Window opacity", opacity);
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target IDA
```

Expected: clean build.

- [ ] **Step 4: Run the full test suite to confirm no regressions**

```bash
ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j
```

Expected: 775 / 775 (baseline 773 + 1 from Task 6 + 2 from Task 7). Allow the 3 known flakes to retry once.

- [ ] **Step 5: Commit**

```bash
git add app/FileInputPlayerWindow.cpp
git commit -m "feat: app — file-input player window pins via 'Always on top' menu toggle (persisted)"
git push origin master
```

---

## Wrap-up: spec correction + continue.md refresh + memory

These steps land after Task 8 to keep the artifact set consistent.

- [ ] **Step 1: Correct spec test-paths footnote**

In `docs/superpowers/specs/2026-05-26-file-input-player-window-polish-design.md`, in §4 the file surface lists `audio/tests/FileInputPersistenceTests.cpp` and `audio/tests/FileInputRegistryTests.cpp`. Actual test location is `tests/` (top-level). Update both paths in §4:

```
tests/FileInputPersistenceTests.cpp     round-trip alwaysOnTop; missing-key defaults to false
tests/FileInputRegistryTests.cpp        setFileInputAlwaysOnTop updates descriptor
```

- [ ] **Step 2: Refresh `continue.md`**

Replace `continue.md` with a fresh session-handoff summarizing the slice: 8 commits (T1-T8), test count moved from 773 → 775, the four 2026-05-26 follow-ons are now (B) Transport sync, (C) MIDI, (D) Video — three remaining instead of four. Reference the spec + this plan. Per `feedback_update_continue_md_every_session`.

- [ ] **Step 3: Commit the spec + continue.md update**

```bash
git add docs/superpowers/specs/2026-05-26-file-input-player-window-polish-design.md continue.md
git commit -m "docs: continue.md + spec footnote — player-window-polish slice closed; 3 follow-ons remaining"
git push origin master
```

---

## Self-review summary

**Spec coverage:**
- §3.1 chrome flags → Task 4
- §3.2 drag-to-move → Task 3
- §3.3 bg-only opacity → Task 4 (paint override + setAlpha removal)
- §3.4 close gestures → Task 1 (Cmd-W) + Task 2 (menu item)
- §3.5 always-on-top → Tasks 5 + 6 + 7 + 8 (field, setter, persistence, apply/menu)
- §3.6 cross-platform → no task needed (macOS-only this milestone per spec)
- §4 file surface → enumerated per task; correction to test-paths covered in wrap-up
- §5 commit slicing → spec said "2 commits"; plan uses 8 single-task commits to match IDA's audio-routing-slice precedent. Same logical slicing, finer-grained commits.
- §6 verification → Task 4 step 5 + Task 6 step 5/6 + Task 7 step 7/8 + Task 8 step 4 run the test suite at the right beats.
- §7 risks → addressed by ordering (close + drag before title-bar drop) and by the spec's documented rollback path.

**No placeholders.** Every step has exact code or exact commands.

**Type consistency:** `alwaysOnTop` (bool, false default), `setFileInputAlwaysOnTop(InputId, bool)`, `juce::ComponentDragger dragger_;` — same name used everywhere it appears. Menu item label `"Always on top"` consistent across menu definition and any future doc.
