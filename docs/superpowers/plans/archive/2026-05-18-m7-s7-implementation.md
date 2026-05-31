# M7 S7 — MainComponent production wiring of OutOfProcess plug-in editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `MainComponent` so the operator can open a scanned CLAP plug-in's editor in a floating `juce::DocumentWindow`, exercising the S3–S6 out-of-process plug-in stack end-to-end. Each "Open editor" click spawns a `ida_plugin_host` child on a scratch busId, brings up `OutOfProcessEditorView` inside a `DocumentWindow`, and close-box tears the slot down. Multiple plug-ins loadable simultaneously. No audio routing.

**Architecture:** `OutOfProcessEffectChainHost` lives as a `MainComponent` member; `PluginsPane` swaps its read-only `juce::TextEditor` for a `juce::ListBox` with per-row "Open editor" buttons; each click allocates a fresh scratch busId (1000+), calls `configureBus`, and floats a `PluginEditorWindow` (new `juce::DocumentWindow` subclass) owning an `OutOfProcessEditorView`. Close-button tears down. Engine audio output unaffected (scratch chains aren't in the OutputMixer graph).

**Tech Stack:** C++20, JUCE (`juce::DocumentWindow`, `juce::ListBox`, `juce::ListBoxModel`, `juce::NSViewComponent`), `Ida::Host` library (existing), `Ida::Core` (existing). No new external deps.

**Spec:** `docs/superpowers/specs/2026-05-18-m7-s7-design.md` (commit `5c94bd5`).

**Boundary:** No audio-thread surface added; `RT_SAFETY_CONTRACT.md` unchanged. The chain host is GUI-eyes-on only — `pumpSlot` is never invoked for scratch buses because the OutputMixer doesn't know about them.

---

## File map

**New files:**
- `tests/MainComponentPluginEditorTests.cpp` — three lifecycle tests

**Modified files:**
- `app/MainComponent.h` — add OutOfProcessEffectChainHost + editorWindows_ + openPluginEditor/closePluginEditor + kScratchBusIdBase
- `app/MainComponent.cpp` — PluginListBox inner class, PluginEditorWindow inner class, openPluginEditor / closePluginEditor bodies, PluginsPane TextEditor → ListBox swap, ctor wiring + dtor teardown, hostBinary() resolver
- `tests/CMakeLists.txt` — register MainComponentPluginEditorTests.cpp into IdaTests
- `docs/operator/m7-eyes-on.md` — replace the "M20+ debug menu" paragraph with the actual S7 workflow
- `continue.md` — S7 close-out + S8 handoff at session end

**Unchanged (re-verified during execution):**
- `host/include/ida/OutOfProcessEffectChainHost.h` — public API consumed as-is
- `host/include/ida/OutOfProcessEditorView.h` — same
- `app/CMakeLists.txt` — `Ida::Host` already linked from S6
- All S6 XPC / CARemoteLayer code — unchanged
- `RT_SAFETY_CONTRACT.md` — unchanged

**Commit cadence:** one commit per task. Single-line messages, project convention `<type>: <short title>`. Final feature commit at end is `feat: M7 S7 — MainComponent wires OutOfProcessEffectChainHost + floating plug-in editor windows`.

**Push policy:** commit only inside each task. The final push to `origin/master` happens in Task 7 (close-out).

---

## Task 1: Add OutOfProcessEffectChainHost member + helpers + ctor/dtor wiring (no UI yet)

Lay the foundation: MainComponent owns the chain host and resolves the host-binary path. No "Open editor" button yet; this task just proves the chain host can live as a MainComponent member without breaking the existing test suite.

**Files:**
- Modify: `app/MainComponent.h`
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Read the existing MainComponent.h private-member layout**

Run: `sed -n '175,242p' app/MainComponent.h` and find the block after the existing `PluginScanner pluginScanner_;` member (around line 193) — this is where the new chain-host members go.

Run: `grep -n "MainComponent (" app/MainComponent.cpp | head -3` to find the ctor.
Run: `grep -n "~MainComponent" app/MainComponent.cpp | head -3` to find the dtor.

- [ ] **Step 2: Add OutOfProcessEffectChainHost include + members in MainComponent.h**

In `app/MainComponent.h`, near the top with the other `#include "ida/..."` lines, add:

```cpp
#include "ida/OutOfProcessEffectChainHost.h"
```

In the private section after `std::unique_ptr<juce::FileChooser> pluginFolderChooser_;`, add a new block:

```cpp
    // --- M7 S7: out-of-process plug-in editor hosting -----------------------
    // Scratch busIds for editor-only plug-in loads. The OutputMixer is NOT
    // wired to know about these buses, so no audio routes through them. The
    // operator gets a GUI surface; engine audio output is unaffected. Real
    // per-bus chain integration is a later session.
    static constexpr std::int64_t kScratchBusIdBase = 1000;

    ida::OutOfProcessEffectChainHost effectChainHost_;
    std::int64_t                        nextScratchBusId_ { kScratchBusIdBase };

    class PluginEditorWindow;
    std::vector<std::unique_ptr<PluginEditorWindow>> editorWindows_;

    /// Resolves Contents/MacOS/ida_plugin_host alongside the running app
    /// binary. Returns an invalid juce::File outside a .app bundle (dev-loop
    /// runs from build/...); callers must check `existsAsFile()` before use.
    juce::File hostBinaryPath() const;

    /// Spawns a ida_plugin_host child via configureBus on a fresh scratch
    /// busId, and floats a PluginEditorWindow showing the editor.
    /// Message-thread only.
    void openPluginEditor (const PluginDescriptor& descriptor);

    /// Tears down the slot at `busId` and removes the matching window.
    /// Message-thread only. Safe to call from a window's close-button callback
    /// via juce::MessageManager::callAsync.
    void closePluginEditor (std::int64_t busId);
```

- [ ] **Step 3: Implement hostBinaryPath() in MainComponent.cpp**

Find a quiet spot in `app/MainComponent.cpp` (after the existing helper functions, before the `MainComponent::MainComponent(...)` ctor). Add:

```cpp
juce::File MainComponent::hostBinaryPath() const
{
    // Inside a .app bundle this is Contents/MacOS/IDA; the helper
    // sits in the same MacOS directory. Outside a bundle (dev-loop test
    // runs from build/...), the sibling doesn't exist and the returned
    // juce::File reports existsAsFile() == false — callers must check.
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto sibling = self.getParentDirectory().getChildFile ("ida_plugin_host");
    return sibling;
}
```

- [ ] **Step 4: Add forward declaration of PluginEditorWindow in MainComponent.cpp**

PluginEditorWindow's class body lands in Task 4. Until then, the destructor in Task 1 needs `unique_ptr<PluginEditorWindow>` to be destructible — which means the inner class must be defined before MainComponent's dtor. Since the dtor body lives in MainComponent.cpp anyway, this works naturally: just keep PluginEditorWindow declared but undefined in this task; the dtor's `editorWindows_.clear()` won't compile until Task 4 lands.

For Task 1 only: add a stub class definition INSIDE `app/MainComponent.cpp` near the top of the file (before `MainComponent::MainComponent(...)` ctor), so the `unique_ptr` is happy. Task 4 will replace this with the real body:

```cpp
// Task 1 stub: minimal definition so unique_ptr<PluginEditorWindow> is
// destructible. Task 4 replaces this with the real juce::DocumentWindow
// subclass body.
class MainComponent::PluginEditorWindow
{
public:
    PluginEditorWindow() = default;
};
```

- [ ] **Step 5: Find the MainComponent dtor body**

Run: `grep -n "MainComponent::~MainComponent" app/MainComponent.cpp`

If no explicit destructor exists, the implicit one is fine for Task 1 — `effectChainHost_` will destruct after `editorWindows_` because members destruct in reverse declaration order, and we declared `editorWindows_` AFTER `effectChainHost_`. Wait — that's backwards. **Critical:** `editorWindows_` must destruct BEFORE `effectChainHost_` because each window holds an `OutOfProcessEditorView` that polls the host. Re-order in Step 2: `editorWindows_` declared AFTER `effectChainHost_` ensures `editorWindows_` destructs FIRST (members destruct in reverse declaration order).

The plan as written in Step 2 is correct. Verify by re-reading the member order: `effectChainHost_` declared first, then `nextScratchBusId_`, then `editorWindows_`. On destruction: `editorWindows_` first (good — windows close, views destruct, slot polls stop), then `nextScratchBusId_` (trivial), then `effectChainHost_` (its dtor joins the supervisor thread + reaps any remaining children).

- [ ] **Step 6: Build**

Run: `cmake --build build -j 2>&1 | tail -10`
Expected: clean build. `openPluginEditor` + `closePluginEditor` are declared but not defined; that's fine — the compiler won't complain until something calls them (which happens in Task 4+).

If the build fails with "undefined reference to MainComponent::openPluginEditor" or similar, double-check: nothing in Task 1's code should call these — only declarations exist, no callers yet.

- [ ] **Step 7: Run full test suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 384/384 pass. Adding a chain host as a MainComponent member shouldn't affect any existing test (MainComponent isn't constructed by ctest directly — it's only via the IDA.app's `juce_add_gui_app`).

- [ ] **Step 8: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: M7 S7 step 1 — MainComponent owns OutOfProcessEffectChainHost + hostBinaryPath helper"
```

---

## Task 2: Implement openPluginEditor / closePluginEditor with stub PluginEditorWindow

Wire the chain host's `configureBus` lifecycle through the new methods. The window is still a stub (Task 4 fills it in), so the calls don't yet produce visible UI — but the engine-side IPC + supervisor child spawn / reap can be unit-tested.

**Files:**
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Read OutOfProcessEffectChainHost::configureBus signature**

Run: `sed -n '125,142p' host/include/ida/OutOfProcessEffectChainHost.h`

Expected signature:
```cpp
void configureBus (std::int64_t busId,
                   const EffectChain& chain,
                   const juce::File& hostBinary,
                   const juce::File& clapBundle);
```

The `clapBundle` argument is the path to the .clap file (the descriptor's `filePath`). The `chain` is an `EffectChain` containing one `EffectChainEntry` whose `descriptor` references that bundle.

- [ ] **Step 2: Include the EffectChain header in MainComponent.cpp**

If not already included via the existing includes, add:

```cpp
#include "ida/EffectChain.h"
```

(Likely already included transitively; verify via `grep -n "EffectChain" app/MainComponent.cpp`.)

- [ ] **Step 3: Implement openPluginEditor body**

Add to `app/MainComponent.cpp` (after the existing PluginsPane class or near the existing plug-in scan methods):

```cpp
void MainComponent::openPluginEditor (const PluginDescriptor& descriptor)
{
    const auto hostBinary = hostBinaryPath();
    if (! hostBinary.existsAsFile())
    {
        // Dev-loop or broken bundle. The UI button should be disabled in
        // this case (Task 5 handles enable/disable + tooltip), so reaching
        // here is a contract violation. Bail silently rather than spawning
        // a child that will fail.
        return;
    }

    const juce::File clapBundle (juce::String (descriptor.filePath));
    if (! clapBundle.exists())
        return;

    const auto busId = nextScratchBusId_++;

    ida::EffectChainEntry entry;
    entry.descriptor  = descriptor;
    entry.displayName = descriptor.name;
    entry.bypassed    = false;
    ida::EffectChain chain;
    chain = chain.withAppended (entry);

    effectChainHost_.configureBus (busId, chain, hostBinary, clapBundle);

    // Task 4 will replace this stub with a real PluginEditorWindow body
    // that owns an OutOfProcessEditorView at the given busId.
    auto window = std::make_unique<PluginEditorWindow>();
    editorWindows_.push_back (std::move (window));
}
```

- [ ] **Step 4: Implement closePluginEditor body**

Add immediately after:

```cpp
void MainComponent::closePluginEditor (std::int64_t busId)
{
    // Empty chain → host's stale-slot eraser removes the slot in
    // configureBus, supervisor reaps the child.
    const auto hostBinary = hostBinaryPath();
    effectChainHost_.configureBus (busId, ida::EffectChain{}, hostBinary, juce::File{});

    // Task 4 will tag each PluginEditorWindow with its busId so we can
    // find + remove the matching one. For Task 2's stub, we just clear
    // all windows whenever any close fires — Task 4 fixes this.
    editorWindows_.clear();
}
```

(The "clear all on any close" is intentionally crude here — Task 4 introduces the per-window busId tagging that makes targeted removal possible. Task 2 only needs the lifecycle to compile + the chain-host calls to fire.)

- [ ] **Step 5: Build**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: clean build.

- [ ] **Step 6: Run full test suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 384/384 pass. The new methods are message-thread only and have no callers yet.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: M7 S7 step 2 — MainComponent::openPluginEditor / closePluginEditor lifecycle (stub window)"
```

---

## Task 3: PluginListBox inner class — replace PluginsPane TextEditor with a ListBox + Open editor buttons

Swap the existing read-only `juce::TextEditor descriptorsList_` for a `juce::ListBox` whose rows each have an "Open editor" button. Failed-files section becomes a separate label below. No callback wired yet (Task 5 connects buttons to `openPluginEditor`); this task is purely the UI swap.

**Files:**
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Read the existing PluginsPane definition**

Run: `sed -n '450,535p' app/MainComponent.cpp` to see the PluginsPane class with its `descriptorsList_` TextEditor + `setDescriptors` method.

- [ ] **Step 2: Add PluginListBox inner class definition**

In `app/MainComponent.cpp`, immediately BEFORE `class MainComponent::PluginsPane final : public juce::Component` (around line 452), add a new inner class:

```cpp
// =============================================================================
// PluginListBox — one row per scanned descriptor + an "Open editor" button.
// Replaces the S5-era read-only TextEditor descriptor dump (M7 S7).
// =============================================================================
class MainComponent::PluginsPane::PluginListBox final
    : public juce::ListBoxModel
{
public:
    /// Callback fired when a row's "Open editor" button is clicked.
    /// Message-thread only; wired by MainComponent::PluginsPane.
    std::function<void(const PluginDescriptor&)> onOpenEditor;

    /// Called by MainComponent when hostBinaryPath() changes (in practice
    /// once, in ctor). When invalid, all "Open editor" buttons are
    /// disabled with a tooltip explaining why.
    void setHostBinaryAvailable (bool available)
    {
        hostBinaryAvailable_ = available;
    }

    void setDescriptors (const std::vector<PluginDescriptor>& descriptors)
    {
        descriptors_ = descriptors;
    }

    const std::vector<PluginDescriptor>& descriptors() const noexcept
    {
        return descriptors_;
    }

    bool hostBinaryAvailable() const noexcept { return hostBinaryAvailable_; }

    // ---- juce::ListBoxModel ---------------------------------------------
    int getNumRows() override { return (int) descriptors_.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g,
                           int width, int height, bool /*rowIsSelected*/) override
    {
        if (rowNumber < 0 || rowNumber >= (int) descriptors_.size())
            return;
        const auto& d = descriptors_[(std::size_t) rowNumber];
        g.fillAll (rowNumber % 2 ? juce::Colour (0xff1a1a1a)
                                 : juce::Colour (0xff222222));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (d.name) + "   (" + juce::String (d.manufacturer) + ")",
                    8, 4, width - 110, 18, juce::Justification::topLeft);
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, 0));
        g.drawText (juce::String (d.uniqueId),
                    8, 22, width - 110, 14, juce::Justification::topLeft);
        g.setColour (juce::Colour (0xff808080));
        g.drawText (juce::String (d.filePath),
                    8, 36, width - 110, 14, juce::Justification::topLeft);
    }

    juce::Component* refreshComponentForRow (int rowNumber, bool /*rowIsSelected*/,
                                             juce::Component* existing) override
    {
        if (rowNumber < 0 || rowNumber >= (int) descriptors_.size())
        {
            delete existing;
            return nullptr;
        }

        auto* button = dynamic_cast<RowButton*> (existing);
        if (button == nullptr)
        {
            delete existing;
            button = new RowButton();
        }
        button->setRowAndDescriptor (rowNumber, descriptors_[(std::size_t) rowNumber]);
        button->setEnabled (hostBinaryAvailable_);
        button->setTooltip (hostBinaryAvailable_
            ? juce::String()
            : juce::String ("Open editor unavailable — launch the .app for plug-in hosting"));
        button->onClick = [this, rowNumber]
        {
            if (rowNumber >= 0 && rowNumber < (int) descriptors_.size() && onOpenEditor)
                onOpenEditor (descriptors_[(std::size_t) rowNumber]);
        };
        return button;
    }

    static constexpr int kRowHeight = 56;

private:
    class RowButton final : public juce::TextButton
    {
    public:
        RowButton() : juce::TextButton ("Open editor") {}
        void setRowAndDescriptor (int row, const PluginDescriptor&)
        {
            rowNumber_ = row;
        }
        void resized() override
        {
            // Right-aligned inside the row; ~88 px wide, vertically centred.
            const auto bounds = getParentComponent() != nullptr
                ? juce::Rectangle<int> (getParentWidth() - 96,
                                        (kRowHeight - 24) / 2,
                                        88, 24)
                : juce::Rectangle<int> (0, 0, 88, 24);
            setBounds (bounds);
        }
    private:
        int rowNumber_ { -1 };
    };

    std::vector<PluginDescriptor> descriptors_;
    bool                          hostBinaryAvailable_ { false };
};
```

- [ ] **Step 3: Replace the TextEditor with a ListBox in PluginsPane**

Find the PluginsPane class body (around line 452). Replace the `juce::TextEditor descriptorsList_;` member declaration with:

```cpp
    PluginListBox    listBoxModel_;
    juce::ListBox    descriptorsListBox_ { juce::String(), &listBoxModel_ };
    juce::Label      failedFilesLabel_;
```

Replace the existing constructor body that configures `descriptorsList_`:

```cpp
        descriptorsList_.setMultiLine (true);
        descriptorsList_.setReadOnly (true);
        descriptorsList_.setScrollbarsShown (true);
        descriptorsList_.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, 0));
        addAndMakeVisible (descriptorsList_);
```

with:

```cpp
        descriptorsListBox_.setRowHeight (PluginListBox::kRowHeight);
        descriptorsListBox_.setColour (juce::ListBox::backgroundColourId,
                                       juce::Colour (0xff1a1a1a));
        addAndMakeVisible (descriptorsListBox_);

        failedFilesLabel_.setMinimumHorizontalScale (1.0f);
        failedFilesLabel_.setColour (juce::Label::textColourId,
                                     juce::Colour (0xffff8888));
        failedFilesLabel_.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, 0));
        addAndMakeVisible (failedFilesLabel_);
```

- [ ] **Step 4: Rewrite setDescriptors**

Replace the existing `setDescriptors` body with:

```cpp
    void setDescriptors (const std::vector<PluginDescriptor>& descriptors,
                         const std::vector<std::string>&      failed)
    {
        listBoxModel_.setDescriptors (descriptors);
        descriptorsListBox_.updateContent();

        if (failed.empty())
        {
            failedFilesLabel_.setText (juce::String(), juce::dontSendNotification);
        }
        else
        {
            juce::String text = "Failed to load:\n";
            for (const auto& f : failed)
                text << "  " << juce::String (f) << "\n";
            failedFilesLabel_.setText (text, juce::dontSendNotification);
        }
    }
```

- [ ] **Step 5: Update resized() to lay out the new components**

Replace the existing `resized()` body in PluginsPane with:

```cpp
    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        headerLabel_.setBounds (area.removeFromTop (24));
        formatsLabel_.setBounds (area.removeFromTop (22));
        area.removeFromTop (8);
        scanButton_.setBounds (area.removeFromTop (28).reduced (0, 2));
        scanStatusLabel_.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);

        // Failed-files label takes the bottom strip; list box fills the rest.
        auto failedArea = area.removeFromBottom (
            failedFilesLabel_.getText().isEmpty() ? 0 : 60);
        failedFilesLabel_.setBounds (failedArea);
        descriptorsListBox_.setBounds (area);
    }
```

- [ ] **Step 6: Build**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: clean build. If the build fails on `friend class MainComponent;` references to the old `descriptorsList_` or `scanButton_`, those friend-accesses still work; the names changed but the friend access is whole-class.

- [ ] **Step 7: Run full test suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 384/384 pass.

- [ ] **Step 8: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: M7 S7 step 3 — PluginsPane uses ListBox with per-row Open-editor buttons"
```

---

## Task 4: Real PluginEditorWindow + per-window busId tagging + targeted removal

Replace the Task 1 stub PluginEditorWindow with a real `juce::DocumentWindow` subclass that owns an `OutOfProcessEditorView`. Fix `closePluginEditor` to remove only the matching window (the Task 2 "clear all" was deliberately crude).

**Files:**
- Modify: `app/MainComponent.h`
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Add OutOfProcessEditorView include**

In `app/MainComponent.h`, add near the existing `#include "ida/..."` lines:

```cpp
#include "ida/OutOfProcessEditorView.h"
```

- [ ] **Step 2: Replace the Task 1 stub PluginEditorWindow with the real body**

In `app/MainComponent.cpp`, find the Task 1 stub:

```cpp
class MainComponent::PluginEditorWindow
{
public:
    PluginEditorWindow() = default;
};
```

Replace with the real implementation:

```cpp
// =============================================================================
// PluginEditorWindow — floating juce::DocumentWindow that owns one
// OutOfProcessEditorView bound to a scratch busId (M7 S7).
// =============================================================================
class MainComponent::PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow (const juce::String&                  title,
                        std::int64_t                          busId,
                        ida::OutOfProcessEffectChainHost&  host,
                        std::function<void(std::int64_t)>     onClose)
        : juce::DocumentWindow (title,
                                juce::Colours::black,
                                juce::DocumentWindow::allButtons),
          busId_   (busId),
          onClose_ (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, /*useBottomRightCornerResizer*/ false);

        // Initial size — replaced by the plug-in's preferred size once the
        // editor view's polling timer detects the child's first contextId
        // publish (typically <100 ms after spawn).
        auto* view = new ida::OutOfProcessEditorView (host, busId, /*slot*/ 0);
        view->setSize (600, 400);
        setContentOwned (view, /*resizeToFitContent*/ true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    std::int64_t busId() const noexcept { return busId_; }

    void closeButtonPressed() override
    {
        // Defer the actual teardown so we don't destroy ourselves from
        // inside JUCE's event dispatcher.
        if (onClose_)
        {
            const auto busId = busId_;
            auto cb = onClose_;
            juce::MessageManager::callAsync ([cb, busId] { cb (busId); });
        }
    }

private:
    std::int64_t                       busId_;
    std::function<void(std::int64_t)>  onClose_;
};
```

- [ ] **Step 3: Update openPluginEditor to construct the real window**

Find the Task 2 stub `openPluginEditor` body and replace its window-construction block:

```cpp
    // Task 4 will replace this stub with a real PluginEditorWindow body
    // that owns an OutOfProcessEditorView at the given busId.
    auto window = std::make_unique<PluginEditorWindow>();
    editorWindows_.push_back (std::move (window));
```

with:

```cpp
    auto window = std::make_unique<PluginEditorWindow> (
        juce::String (descriptor.name),
        busId,
        effectChainHost_,
        [this] (std::int64_t b) { closePluginEditor (b); });
    editorWindows_.push_back (std::move (window));
```

- [ ] **Step 4: Fix closePluginEditor for targeted removal**

Replace the Task 2 `editorWindows_.clear()` with targeted removal:

```cpp
void MainComponent::closePluginEditor (std::int64_t busId)
{
    const auto hostBinary = hostBinaryPath();
    effectChainHost_.configureBus (busId, ida::EffectChain{}, hostBinary, juce::File{});

    editorWindows_.erase (
        std::remove_if (editorWindows_.begin(), editorWindows_.end(),
            [busId] (const std::unique_ptr<PluginEditorWindow>& w)
            {
                return w != nullptr && w->busId() == busId;
            }),
        editorWindows_.end());
}
```

(Add `#include <algorithm>` near the top of MainComponent.cpp if not already present.)

- [ ] **Step 5: Build**

Run: `cmake --build build -j 2>&1 | tail -10`
Expected: clean build.

- [ ] **Step 6: Run full test suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 384/384 pass.

- [ ] **Step 7: Commit**

```bash
git add app/MainComponent.h app/MainComponent.cpp
git commit -m "feat: M7 S7 step 4 — PluginEditorWindow (juce::DocumentWindow) owns OutOfProcessEditorView + busId-targeted close"
```

---

## Task 5: Wire the PluginListBox button callback to openPluginEditor

Connect the per-row "Open editor" button to `MainComponent::openPluginEditor`, and set the host-binary-available flag so buttons enable/disable correctly.

**Files:**
- Modify: `app/MainComponent.cpp`

- [ ] **Step 1: Find the PluginsPane construction site in MainComponent ctor**

Run: `grep -n "pluginsPane_ = std::make_unique" app/MainComponent.cpp`

Expected: a line like `pluginsPane_ = std::make_unique<PluginsPane>();` around line 728.

- [ ] **Step 2: Wire the listBoxModel_ onOpenEditor callback + setHostBinaryAvailable**

Immediately after `pluginsPane_ = std::make_unique<PluginsPane>();`, before the existing `pluginsPane_->scanButton_.onClick = ...` line, add:

```cpp
    pluginsPane_->listBoxModel_.onOpenEditor =
        [this] (const PluginDescriptor& d) { openPluginEditor (d); };
    pluginsPane_->listBoxModel_.setHostBinaryAvailable (
        hostBinaryPath().existsAsFile());
```

(Access via `pluginsPane_->listBoxModel_` works because PluginListBox is declared inside PluginsPane and `friend class MainComponent;` already exists in PluginsPane — verify; if it doesn't, the existing `scanButton_.onClick` line would also fail, so the friend access is already established.)

- [ ] **Step 3: Build + smoke**

Run: `cmake --build build -j 2>&1 | tail -5`
Expected: clean build.

If the friend access doesn't reach into the nested PluginListBox, add a small forwarding method on PluginsPane:

```cpp
public:
    void setOnOpenEditor (std::function<void(const PluginDescriptor&)> cb)
    {
        listBoxModel_.onOpenEditor = std::move (cb);
    }
    void setHostBinaryAvailable (bool available)
    {
        listBoxModel_.setHostBinaryAvailable (available);
    }
```

…and call `pluginsPane_->setOnOpenEditor(...)` / `pluginsPane_->setHostBinaryAvailable(...)` from the ctor instead. Pick whichever path the existing code prefers (the existing `scanButton_.onClick = ...` accesses suggest direct friend access is the convention).

- [ ] **Step 4: Run full test suite**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: 384/384 pass.

- [ ] **Step 5: Launch the .app for a sanity smoke**

The dev-loop runs from `build/app/IDA_artefacts/Release/IDA.app` (or `Debug/` depending on generator). Per memory `feedback_can_launch_app`, Claude is authorized to launch:

```bash
open "build/app/IDA_artefacts/Release/IDA.app"
```

Wait 3 seconds, then verify no orphan processes from previous tests:

```bash
ps -A | grep -E "IDA|sirius_(plugin_host|gui_bridge)" | grep -v grep
```

Expected: ONLY `IDA` running (no plug-in spawned yet because no descriptor has been clicked). Quit:

```bash
osascript -e 'quit app "IDA"'
sleep 1
ps -A | grep -E "IDA|sirius_(plugin_host|gui_bridge)" | grep -v grep
```

Expected: nothing returned (clean shutdown). If `sirius_gui_bridge` lingers, that's a launchd-managed XPC service that may persist for a few seconds — re-check after 5 seconds.

- [ ] **Step 6: Commit**

```bash
git add app/MainComponent.cpp
git commit -m "feat: M7 S7 step 5 — wire PluginListBox Open-editor button → openPluginEditor"
```

---

## Task 6: Headless lifecycle tests + operator docs refresh

Add three `[main-component-plugin-editor]` ctest cases that verify the MainComponent ↔ chain-host lifecycle (without rendering pixels), and refresh `docs/operator/m7-eyes-on.md` to use the new "Open editor" workflow.

**Files:**
- Create: `tests/MainComponentPluginEditorTests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/operator/m7-eyes-on.md`

- [ ] **Step 1: Inspect existing MainComponent test patterns**

Run: `grep -rln "MainComponent" tests/ | head` to see if any existing tests construct MainComponent. If yes, follow their pattern. If no (likely), the new tests are the first; we construct MainComponent inside a `juce::ScopedJuceInitialiser_GUI` and pump the message manager.

- [ ] **Step 2: Create `tests/MainComponentPluginEditorTests.cpp`**

```cpp
// =============================================================================
// MainComponentPluginEditorTests.cpp — MainComponent ↔ OutOfProcessEffectChain
// lifecycle tests (M7 S7).
// =============================================================================
// Verifies the chain-host lifetime inside MainComponent (ctor + dtor), and
// the openPluginEditor / closePluginEditor pair using the synthetic CLAP
// plug-in's .clap bundle. Does NOT render pixels (CI is headless); the
// operator eyes-on procedure verifies the actual cross-process compositing.
//
// Tag: [main-component-plugin-editor]

#include "MainComponent.h"
#include "ida/PluginDescriptor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#ifdef __APPLE__

namespace
{
    juce::File hostBinaryForTesting()
    {
        if (auto* envPath = std::getenv ("IDA_PLUGIN_HOST_PATH"))
            return juce::File (juce::String (envPath));
        return juce::File();
    }

    juce::File syntheticClapForTesting()
    {
        if (auto* envPath = std::getenv ("IDA_SYNTHETIC_CLAP_PATH"))
            return juce::File (juce::String (envPath));
        return juce::File();
    }
}

TEST_CASE ("MainComponent constructs + destructs cleanly with no editor windows",
           "[main-component-plugin-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    {
        juce::AudioDeviceManager dm;
        MainComponent component (dm);
        CHECK (true); // dtor about to run; assertion is "no crash"
    }
}

TEST_CASE ("openPluginEditor on synthetic descriptor spawns a child + window",
           "[main-component-plugin-editor]")
{
    const auto binary = hostBinaryForTesting();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    const auto bundle = syntheticClapForTesting();
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at IDA_SYNTHETIC_CLAP_PATH");

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioDeviceManager dm;
    MainComponent component (dm);

    ida::PluginDescriptor descriptor;
    descriptor.format       = ida::PluginFormat::CLAP;
    descriptor.uniqueId     = "com.ida.synthetic.test";
    descriptor.name         = "Synthetic Test Plug-in";
    descriptor.manufacturer = "IDA";
    descriptor.filePath     = bundle.getFullPathName().toStdString();

    // openPluginEditor uses MainComponent::hostBinaryPath() which resolves
    // from the running binary. Override via the test-only seam: temporarily
    // set the env var the production helper consults; if no seam exists,
    // this test must be a no-op SKIP. (Detect by checking hostBinaryPath.)
    if (! component.hostBinaryPathForTesting().existsAsFile())
        SKIP ("hostBinaryPath() not resolvable from test binary location");

    component.openPluginEditorForTesting (descriptor);
    CHECK (component.editorWindowCountForTesting() == 1);

    const auto childPid = component.childPidForTestingAtBusId (1000);
    CHECK (childPid > 0);

    component.closePluginEditorForTesting (1000);
    CHECK (component.editorWindowCountForTesting() == 0);
}

TEST_CASE ("closePluginEditor tears down child + window",
           "[main-component-plugin-editor]")
{
    const auto binary = hostBinaryForTesting();
    const auto bundle = syntheticClapForTesting();
    if (! binary.existsAsFile() || ! bundle.isDirectory())
        SKIP ("synthetic test plug-in fixtures unavailable");

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioDeviceManager dm;
    MainComponent component (dm);

    if (! component.hostBinaryPathForTesting().existsAsFile())
        SKIP ("hostBinaryPath() not resolvable from test binary location");

    ida::PluginDescriptor descriptor;
    descriptor.format       = ida::PluginFormat::CLAP;
    descriptor.uniqueId     = "com.ida.synthetic.test";
    descriptor.name         = "Synthetic Test Plug-in";
    descriptor.manufacturer = "IDA";
    descriptor.filePath     = bundle.getFullPathName().toStdString();

    component.openPluginEditorForTesting (descriptor);
    REQUIRE (component.editorWindowCountForTesting() == 1);

    const auto busId = component.firstOpenBusIdForTesting();
    component.closePluginEditorForTesting (busId);

    CHECK (component.editorWindowCountForTesting() == 0);

    // Give the supervisor a moment to reap the child.
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    CHECK (component.childPidForTestingAtBusId (busId) < 0);
}

#endif // __APPLE__
```

**Note:** the test uses `*ForTesting()` accessor names. These need to be added to `MainComponent` — see Step 3.

- [ ] **Step 3: Add test-only accessors to MainComponent**

In `app/MainComponent.h`, add to the public section (above the private members):

```cpp
    // Test-only accessors (M7 S7). Exposed because MainComponentPluginEditorTests
    // exercises the openPluginEditor / closePluginEditor lifecycle from a
    // headless harness; these are not part of the operator surface.
    juce::File   hostBinaryPathForTesting()         const { return hostBinaryPath(); }
    std::size_t  editorWindowCountForTesting()      const noexcept { return editorWindows_.size(); }
    std::int64_t firstOpenBusIdForTesting()         const noexcept;
    std::int64_t childPidForTestingAtBusId (std::int64_t busId) const noexcept;
    void         openPluginEditorForTesting  (const PluginDescriptor& d) { openPluginEditor  (d); }
    void         closePluginEditorForTesting (std::int64_t busId)        { closePluginEditor (busId); }
```

In `app/MainComponent.cpp`, add the body for the two non-trivial accessors:

```cpp
std::int64_t MainComponent::firstOpenBusIdForTesting() const noexcept
{
    if (editorWindows_.empty())
        return -1;
    return editorWindows_.front()->busId();
}

std::int64_t MainComponent::childPidForTestingAtBusId (std::int64_t busId) const noexcept
{
    return effectChainHost_.childPidForTestingAtSlot (busId, 0);
}
```

(`childPidForTestingAtSlot` is the existing accessor on OutOfProcessEffectChainHost — added in S4.)

- [ ] **Step 4: Register the test in tests/CMakeLists.txt**

Read `tests/CMakeLists.txt` and find the IdaTests source list. Add `MainComponentPluginEditorTests.cpp` to it, following the existing pattern. The test file uses JUCE GUI symbols (juce::ScopedJuceInitialiser_GUI, juce::AudioDeviceManager, MainComponent) — the test link line must include the JUCE modules MainComponent depends on.

Check via `grep -n "MainComponent\|juce_gui_basics\|juce_audio_devices" tests/CMakeLists.txt`. If IdaTests doesn't currently link the JUCE modules MainComponent needs, the safest path is to add the new test file as its own executable:

```cmake
if(APPLE)
    sirius_add_catch2_test (MainComponentPluginEditorTests
                            MainComponentPluginEditorTests.cpp)
    target_link_libraries (MainComponentPluginEditorTests PRIVATE
        Ida::Host
        IdaAppCore
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_extra)
    target_include_directories (MainComponentPluginEditorTests PRIVATE
        ${CMAKE_SOURCE_DIR}/app)
endif()
```

(Use whatever macro `tests/CMakeLists.txt` uses for Catch2 test registration — likely a project-local helper. If the existing pattern is a single IdaTests mega-executable that already links everything, just append the source file.)

- [ ] **Step 5: Pass test environment paths via ctest**

Existing `OutOfProcessEditorTests.cpp` uses `IDA_PLUGIN_HOST_PATH` and `IDA_SYNTHETIC_CLAP_PATH` env vars set by CMake. Verify these are already set for IdaTests via `grep -n "IDA_PLUGIN_HOST_PATH\|IDA_SYNTHETIC_CLAP_PATH" tests/CMakeLists.txt`. If yes, the new file inherits them. If no, the new test gracefully SKIPs.

- [ ] **Step 6: Build + run new tests**

```bash
cmake --build build -j 2>&1 | tail -10
ctest --test-dir build -R "main-component-plugin-editor" --output-on-failure 2>&1 | tail -15
```

Expected: 3 tests pass on macOS (or SKIP if fixtures not found, which is acceptable — the operator eyes-on still covers the path).

- [ ] **Step 7: Refresh `docs/operator/m7-eyes-on.md`**

Read the current file. Find the step that says "M20+ adds a 'Add plug-in' UI". Replace that paragraph with:

```markdown
4. Open the app's **Plugins** tab. If no descriptors are listed, click **"Scan a plugin folder..."** and pick a folder containing a CLAP plug-in (the synthetic test plug-in builds at `build/tests/fixtures/SyntheticTestPlugin.clap` and lives in `build/tests/fixtures/` — use that folder for the smoke).

5. Click the **Open editor** button on a descriptor row. A floating window appears with the plug-in's editor. The colour the operator sees tells the story:

   - **A flat coloured rectangle (purple-ish, hue shifts per restart)** — bridge is missing, child fell back to the S5 placeholder. Open Console.app and filter for `ida_plugin_host` — the line `XPC bridge timeout (250 ms); falling back to S5 placeholder editor surface` should be visible.
   - **The synthetic plug-in's actual NSView contents** — success. CARemoteLayer is composing the child's CALayer tree into the engine's window via the window-server.

6. Verify the supervisor-restart re-publication path (optional):
```

(Append the existing supervisor-restart `killall` instructions as step 6.)

- [ ] **Step 8: Full test suite + commit**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: 387/387 pass (384 from S6 + 3 new).

```bash
git add tests/MainComponentPluginEditorTests.cpp tests/CMakeLists.txt \
        app/MainComponent.h app/MainComponent.cpp \
        docs/operator/m7-eyes-on.md
git commit -m "test: M7 S7 step 6 — MainComponent plug-in editor lifecycle tests + eyes-on doc refresh"
```

---

## Task 7: Final integration verify + continue.md handoff + push

Roll up all per-step commits with a final clean rebuild + suite pass + .app smoke + continue.md update. Push to `origin/master`.

**Files:**
- Modify: `continue.md`

- [ ] **Step 1: Clean rebuild**

Per memory `feedback_clean_builds`:

```bash
rm -rf build
cmake -B build -S .
cmake --build build -j 2>&1 | tail -5
```

Expected: zero errors.

- [ ] **Step 2: Full ctest**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: **387/387** pass. If the count differs, investigate before continuing.

- [ ] **Step 3: Verify bundle layout still intact**

```bash
find "build/app/IDA_artefacts" -path "*IDA.app/Contents/*" \
    \( -name "ida_plugin_host" -o -name "*.xpc" \) | sort
```

Expected: both helpers still present (S6 wiring preserved).

- [ ] **Step 4: Launch the app + sanity check**

```bash
open "build/app/IDA_artefacts/Release/IDA.app"
sleep 3
ps -A | grep -E "IDA|sirius_(plugin_host|gui_bridge)" | grep -v grep
```

Expected: only `IDA` (no plug-in spawned yet). Quit:

```bash
osascript -e 'quit app "IDA"'
sleep 2
ps -A | grep -E "IDA|sirius_(plugin_host|gui_bridge)" | grep -v grep
```

Expected: nothing returned. If `sirius_gui_bridge` lingers > 5 seconds, that's a launchd-managed XPC service that may persist briefly — acceptable.

- [ ] **Step 5: Update continue.md**

Replace the current `## RESUME HERE` section with a new S7 close-out section. Move the current S6 section to historical. Template:

```markdown
# Session Continuation — 2026-MM-DD (M7 S7 SHIPPED to origin/master; M7 S8 selection next)

> **For a fresh chat picking this up cold:** read this whole file before
> doing anything. The user's `~/.claude/CLAUDE.md` and the project's
> auto-memory are loaded automatically and contain the rules. This file
> is the state.

---

## RESUME HERE (2026-MM-DD — M7 S7 on origin/master; M7 S8 selection next)

**M7 S7 is on origin/master.** S7 head SHA: `<FILL>`. Per-step commits:

<paste `git log --oneline 5c94bd5..HEAD` here, S7 commits only>

Test count: **387/387** green (was 384 at S6; +3 in S7 — three
`[main-component-plugin-editor]` lifecycle cases).

**S7 made S6's cross-process compositing operator-visible.** The Plugins
tab now shows each scanned descriptor in a `juce::ListBox` with a
per-row "Open editor" button. Clicking spawns a `ida_plugin_host`
child on a scratch busId (1000+) and floats a `juce::DocumentWindow`
containing an `OutOfProcessEditorView`. The close-button tears down the
slot. Multiple plug-ins loadable simultaneously. No audio routes through
the scratch chains — engine output uninterrupted.

### S7-era decisions locked (superset of the S6 list)

(Carry forward S6 items, then add:)

22. **Scratch bus IDs for editor-only loads** (1000+). The OutputMixer
    doesn't know about these buses; the chain host's pumpSlot is never
    called for them. Real per-bus chain integration is a later session.
23. **`MainComponent` owns the `OutOfProcessEffectChainHost`.** Lives
    for the main window's lifetime. `editorWindows_` declared AFTER the
    host so windows destruct first (members destruct in reverse
    declaration order).
24. **Floating DocumentWindow per loaded plug-in** + multi-load.
    DocumentWindow `closeButtonPressed` schedules teardown via
    `juce::MessageManager::callAsync` to avoid use-after-free in the
    JUCE event dispatcher.

### First moves for the M7 S8 chat

S8 candidate: **audio-ring SPSC violation fix** (carryover from S5
deviation #1). Engine's `tryWriteBytes` + `sendBytes` share the
producer side of the engine→host ring. Latent today; becomes a real
bug as soon as the operator opens an editor mid-playback. With S7
shipping the operator-facing editor surface, this is the next thing
that could bite real users.

### Carryover NOT resolved

(Same list as S6 minus the MainComponent wiring item that S7 closed.)
```

- [ ] **Step 6: Commit + push**

Per memory `feedback_claude_commits_and_pushes_master`:

```bash
git add continue.md
git commit -m "docs: continue.md — M7 S7 close-out + M7 S8 handoff"
git push origin master
```

- [ ] **Step 7: Fill the S7 head SHA into continue.md**

```bash
S7_SHA=$(git rev-parse HEAD)
# Edit continue.md to replace <FILL> with $S7_SHA
git add continue.md
git commit -m "docs: continue.md — fill M7 S7 SHA ($S7_SHA)"
git push origin master
```

- [ ] **Step 8: Verify origin in sync**

```bash
git fetch origin && git log --oneline origin/master -5
```

Expected: top entries are the close-out + SHA-fill commits.

---

## Self-review checklist (run after writing all tasks)

**Spec coverage:**
- ✅ OutOfProcessEffectChainHost as MainComponent member (Task 1)
- ✅ hostBinaryPath() resolver (Task 1)
- ✅ openPluginEditor / closePluginEditor lifecycle (Task 2 + Task 4)
- ✅ PluginListBox + per-row Open-editor button (Task 3)
- ✅ Real PluginEditorWindow (juce::DocumentWindow) (Task 4)
- ✅ Wire button → openPluginEditor (Task 5)
- ✅ Test-only accessors + 3 lifecycle tests (Task 6)
- ✅ Operator eyes-on doc refresh (Task 6)
- ✅ continue.md handoff (Task 7)
- ✅ No audio routing (architecturally enforced — scratch busIds aren't in OutputMixer)
- ✅ Multi-load (each Open-editor click → new scratch busId, new window, new child)
- ✅ Close-button tear-down (PluginEditorWindow::closeButtonPressed → MessageManager::callAsync → closePluginEditor)
- ✅ App launch unchanged when no plug-in loaded (Task 5 step 5 smoke verifies)

**Type consistency:**
- `kScratchBusIdBase = 1000` (declared Task 1; used Task 2 + Task 6) — match.
- `nextScratchBusId_` (declared Task 1; used Task 2) — match.
- `effectChainHost_` (declared Task 1; used Task 2 + Task 4 + Task 5 + Task 6 accessors) — match.
- `editorWindows_` (declared Task 1; used Task 2 + Task 4 + accessors) — match.
- `PluginEditorWindow` stub (Task 1) → real body (Task 4) — same name, same nesting.
- `PluginListBox` (Task 3) → `onOpenEditor` callback shape `std::function<void(const PluginDescriptor&)>` (Task 3 + Task 5 wiring) — match.
- `childPidForTestingAtSlot` on OutOfProcessEffectChainHost (existing S4 accessor) — referenced in Task 6 step 3 — verify with `grep` before writing.

**Placeholder scan:**
- "verify by checking" / "verify via grep" — these are read-existing-code-first directives, not actionable placeholders. Acceptable.
- No "TBD" / "implement later" / "fill in details" placeholders in any task step.
