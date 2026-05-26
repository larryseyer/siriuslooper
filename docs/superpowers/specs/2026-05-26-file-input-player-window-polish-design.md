# File-input player window polish — design

**Date:** 2026-05-26
**Status:** spec — awaiting plan
**Predecessor:** `docs/superpowers/specs/2026-05-26-file-input-audio-routing-design.md` (closed at `ebab565`)
**Origin:** operator eyes-on after audio-routing slice landed; player window worked but its chrome read as "a stock OS window" rather than "a floating IDA source." See `todo.md` 2026-05-26 entry.

---

## 1. Goal

Make the file-input player window feel like a floating IDA surface rather than a Mac document window: drop the OS title bar, paint a semi-transparent background that controls read clearly through, and add a persisted "always on top" toggle so the operator can pin a player while working in other windows. Pure UX polish — no engine, signal-path, or persistence-format breaking changes.

## 2. Non-goals

- No layout/feature change to Content (transport row, scrubber, track list, list toolbar are untouched).
- No new opacity concept beyond what already persists today — the existing `windowOpacity` field is repurposed in place; no second field added.
- No change to ownership lifecycle (`MainComponent` still owns the `unique_ptr`; `closeButtonPressed()` still hides; owner still destroys at its slot).
- No iOS work — iOS path is full-screen, none of the chrome concepts apply.
- No Windows/Linux verification this milestone (macOS-first per `feedback_mac_first_linux_windows_last`); the same code paths will exercise on those platforms when their turn comes.

## 3. Design decisions

### 3.1 Title bar — DocumentWindow with `setUsingNativeTitleBar(false)` + `setTitleBarHeight(0)`

Keep `juce::DocumentWindow` as the base class. Two-line change in the ctor disables the native title bar and collapses the chrome row to zero. Edge resize (already `setResizable(true,true)`) keeps working — only the top bar is gone. Surgical: no new base class, no rewiring of close/minimise plumbing, only the visual chrome changes.

### 3.2 Drag-to-move — `juce::ComponentDragger` on Content background

Without a native title bar, the operator needs a way to move the window. The drag region is the **entire Content background** — any click that doesn't land on a child control begins a window drag. Child controls (buttons, scrubber, ListBox) naturally consume their own `mouseDown` first; only unclaimed events reach Content, so the policy lands automatically. Right-click is distinct from drag and continues to open the context menu (see §3.4). No visible drag handle — operator memory `feedback_gesture_over_ui_clutter` (the iPhone strip is already crowded; this window is similar) and `feedback_default_to_professional_elegant` (FL Studio / Reaper floating-tool convention) both point at "entire background drags."

Implementation: a `juce::ComponentDragger` member on `Content`. `Content::mouseDown` keeps the existing popup-menu check (right-click → `showOpacityMenu`, no drag); for left-click it calls `dragger_.startDraggingComponent(getTopLevelComponent(), e)`. `Content::mouseDrag` calls `dragger_.dragComponent(getTopLevelComponent(), e, nullptr)` unconditionally (it only fires after a `startDraggingComponent`). The existing `FileInputPlayerWindow::mouseDown` popup-menu handler stays as the fallback for clicks that somehow bypass Content.

### 3.3 Semi-transparent background — repurpose `windowOpacity` to background-only

Today's `windowOpacity` ∈ [0.5, 1.0] calls `setAlpha()` which dims the **whole** window (controls included). The repurposed semantics: same field, same range, same persistence — but the value now means "background fill alpha; controls stay at 1.0." This matches pro-audio convention (NI / Ableton floating helpers): controls always sharp, background reads through.

Implementation:

- Override `FileInputPlayerWindow::getDesktopWindowStyleFlags()` to OR `juce::ComponentPeer::windowIsSemiTransparent` into `juce::DocumentWindow::getDesktopWindowStyleFlags()`. `TopLevelWindow::recreateDesktopWindow()` (triggered automatically by `setUsingNativeTitleBar(false)` in the ctor) calls this override and creates the peer with the full DocumentWindow flag set plus our semi-transparent flag. (Initial draft prescribed an explicit `addToDesktop(flags)` call, which violates `TopLevelWindow`'s contract — fires a jassert in Debug, strips taskbar/minimise/close/resizable flags in Release. The override pattern is the documented JUCE escape hatch.)
- In the ctor, call `setOpaque(false)` followed by `setBackgroundColour(getBackgroundColour().withAlpha(0.0f))`. The second call is load-bearing: `ResizableWindow::paint` (parent) fills the whole window with its bg colour BEFORE `Content::paint` runs. Without forcing that fill transparent, the parent draws an opaque background that drowns out Content's per-pixel alpha — only the chrome border would appear translucent.
- Override `Content::paint(juce::Graphics&)`: fill `getLocalBounds()` with the look-and-feel's `ResizableWindow::backgroundColourId` at the current `windowOpacity` alpha. (Read the descriptor on each paint — Content already polls the registry at 30 Hz; this is just one more read.)
- Remove every `setAlpha()` call from the opacity menu / Custom… dialog paths (including the ctor-time apply — the per-paint read covers first-paint at the persisted alpha). Replace with `content_->repaint()` so the new alpha lands on the next paint pass.
- The opacity slider live-preview in `showCustomOpacityDialog()` keeps working — it writes the descriptor (or a preview alpha) and triggers a Content repaint instead of `setAlpha`.

Migration: existing sessions with a saved 0.92 now render as "background at 92% alpha, controls at 100%" instead of "everything at 92%." Operator-acceptable behavioral change — better visual, no data loss, no schema change.

### 3.4 Close gesture — right-click menu item + Cmd-W accelerator

Without traffic-light controls, two close paths:

1. The existing right-click context menu (already used for opacity) gains a `"Close window"` item, above the opacity submenu, separated by a divider. Standard `juce::PopupMenu::addItem` calling `closeButtonPressed()`.
2. Standard system close shortcut: `Cmd-W` on macOS, `Ctrl-W` on Win/Linux. JUCE's `DocumentWindow` does **not** auto-bind this — we override `keyPressed(const juce::KeyPress&)` on the window itself, return `true` for `KeyPress('w', juce::ModifierKeys::commandModifier, 0)` and call `closeButtonPressed()`. Window is `setWantsKeyboardFocus(true)` so the key reaches us. `commandModifier` resolves to Cmd on macOS and Ctrl on Win/Linux, so one handler covers all desktop platforms.

No visible × button — the window is intentionally minimal per `feedback_gesture_over_ui_clutter`. Operators who learn the gesture once never look for it again.

### 3.5 Always-on-top — descriptor field, registry setter, menu toggle, persisted

- `core/include/ida/FileInputDescriptor.h` gains `bool alwaysOnTop { false };`.
- `audio/include/ida/FileInputRegistry.h` gains `void setFileInputAlwaysOnTop(InputId, bool);` — mutates descriptor, no audio-thread interaction (windowing is message-thread).
- Right-click menu gains an `"Always on top"` toggle item, ticked when on, calling `registry_.setFileInputAlwaysOnTop(id_, !current)` and `setAlwaysOnTop(newValue)` on this.
- Applied on construction: ctor reads `d->alwaysOnTop` and calls `setAlwaysOnTop(...)` once, alongside the existing opacity read.
- Persisted: `audio/src/FileInputPersistence.cpp` serializes/parses `"alwaysOnTop"`. Missing key → false (back-compat for sessions saved before this slice).

### 3.6 Cross-platform notes

| Platform | Behavior |
|---|---|
| macOS | Full implementation; verified by operator eyes-on. |
| iOS | No-op — the file-input player surface on iOS is its own full-screen view, not a `DocumentWindow`. Nothing in this slice touches iOS code. |
| Windows | Same `DocumentWindow` code path; visual treatment lands for free when Windows reaches the priority list. Not verified this milestone. |
| Linux | Same as Windows. Not verified this milestone. |

---

## 4. File surface

```
core/include/ida/FileInputDescriptor.h        + bool alwaysOnTop { false };

app/FileInputPlayerWindow.h                   + keyPressed override; + getDesktopWindowStyleFlags override;
                                              + showOpacityMenu moved to public (forwarded from Content on right-click)
app/FileInputPlayerWindow.cpp
    ctor:                                     setUsingNativeTitleBar(false)
                                              setTitleBarHeight(0)
                                              setWantsKeyboardFocus(true)
                                              setOpaque(false)
                                              setBackgroundColour(...withAlpha(0.0f))     // parent fill transparent
                                              read d->alwaysOnTop → setAlwaysOnTop(...)
                                              NO setAlpha; NO explicit addToDesktop call
    getDesktopWindowStyleFlags                DocumentWindow::getDesktopWindowStyleFlags() | windowIsSemiTransparent
    Content::paint                            fill bg at windowOpacity alpha (controls paint over at 1.0)
    Content::mouseDown / mouseDrag            ComponentDragger drags parent on left-click background;
                                              right-click forwards to parent's showOpacityMenu()
                                              via findParentComponentOfClass; mouseDrag guarded by isPopupMenu
    showOpacityMenu                           replace setAlpha calls with content_->repaint();
                                              add "Close window" item (top); add "Always on top" toggle item
    showCustomOpacityDialog                   live-preview writes via descriptor + content_->repaint();
                                              cancel reverts via descriptor write + repaint, not setAlpha
    keyPressed                                Cmd-W / Ctrl-W → closeButtonPressed()

audio/include/ida/FileInputRegistry.h         + setFileInputAlwaysOnTop(InputId, bool)
audio/src/FileInputRegistry.cpp               implement setter (descriptor mutation only)

audio/include/ida/FileInputPersistence.h      doc comment: alwaysOnTop in persisted set
audio/src/FileInputPersistence.cpp            serialize alwaysOnTop; parse with default false

tests/FileInputPersistenceTests.cpp           round-trip alwaysOnTop; missing-key defaults to false
tests/FileInputRegistryTests.cpp              setFileInputAlwaysOnTop updates descriptor
```

Chrome / drag / paint / menu / Cmd-W are operator-verified per existing convention (MainComponent / window-class GUI is not unit-tested in IDA). Engine-side persistence + registry setter are unit-tested.

## 5. Plan slicing

Two commits, in the order the todo predicted:

### Commit 1 — chrome + opacity-repurpose

`feat: app — frameless file-input player window with bg-only opacity, drag-to-move, menu/Cmd-W close`

- DocumentWindow chrome flags (title bar off, height 0, semi-transparent peer, keyboard focus)
- Content::paint override (bg fill at windowOpacity alpha)
- ComponentDragger drag-to-move
- Replace `setAlpha()` with `content_->repaint()` everywhere in opacity paths
- Right-click menu gains "Close window" item; Cmd-W / Ctrl-W key handler

No new persisted fields → no new tests required this commit (Content paint + chrome is GUI-verified).

### Commit 2 — always-on-top field + setter + persistence + menu toggle

`feat: file-input — alwaysOnTop persisted field + registry setter + player-window menu toggle`

- `FileInputDescriptor::alwaysOnTop` field
- `FileInputRegistry::setFileInputAlwaysOnTop`
- Persistence read/write with default-false fallback
- Window ctor applies on construction; menu toggle item flips it
- Unit tests: `FileInputPersistenceTests` round-trip + missing-key default; `FileInputRegistryTests` setter mutation

## 6. Verification

- `ctest --test-dir build -E "(PluginEditor|MainComponentPlug)" -j` — baseline 773/773 + 2 new persistence/registry cases = 775/775. Allow the 3 known transient flakes (#279 / #350 / #756) to retry once.
- Operator eyes-on after commit 1: open a file-input player, confirm (a) no native title bar, (b) drag anywhere on background moves the window, (c) right-click → "Close window" hides, (d) Cmd-W hides, (e) opacity slider dims background only (controls stay sharp).
- Operator eyes-on after commit 2: right-click → "Always on top" pins; relaunch the app, confirm the pin survives session reload.

## 7. Risks and rollback

- **JUCE semi-transparent peer flags on macOS:** `windowIsSemiTransparent` is a standard JUCE flag and IDA already uses it elsewhere (verify in passing during commit 1). If macOS layers misbehave, the fallback is to drop the semi-transparent flag and paint a fully-opaque bg at `windowOpacity` — controls stay sharp, but no read-through. Document at commit time if this happens.
- **Drag-vs-control event ordering:** child controls consume their own `mouseDown` first; this is JUCE's default and the existing right-click popup-menu detection in `mouseDown` proves the pattern works. Low risk.
- **Persisted-field forward-compat:** missing `alwaysOnTop` → false is the safe default and matches the existing `windowOpacity` fallback pattern. Zero risk for old sessions.
- **Rollback:** both commits are surgical; `git revert` of either lands cleanly. The opacity-repurpose change is the only behavioral change for existing sessions — if operators dislike controls staying sharp, reverting commit 1 restores whole-window dimming with one revert.

---

*End of spec.*
