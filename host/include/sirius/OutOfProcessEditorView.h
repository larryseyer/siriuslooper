#pragma once

#include "sirius/OutOfProcessEffectChainHost.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sirius
{

/// JUCE `Component` that embeds an out-of-process plug-in editor (M7 S5).
///
/// Wraps a `CALayerHost`-backed `NSView` inside a `juce::NSViewComponent`
/// and tracks the slot's `editorCaContextId()` polled at GUI cadence (30 Hz
/// via `juce::Timer`). When the polled CAContextID changes (initial publish,
/// supervisor restart re-publish), the wrapper points the `CALayerHost` at
/// the new context — the parent process composites the plug-in's CALayer
/// tree directly via the window-server connection. No pixel copying, no
/// focus-routing IPC, no per-frame work on the engine side.
///
/// Lifecycle:
///   - First `parentHierarchyChanged()` (or `visibilityChanged()` to true)
///     calls `OutOfProcessEffectChainHost::requestEditorShow` with the
///     current Component bounds (width × height in points). Lazy — no
///     editor work happens until the wrapper is on-screen.
///   - `resized()` calls `requestEditorResize` so the child plug-in can
///     reflow.
///   - Destruction calls `requestEditorHide`.
///
/// **No `MainComponent` wiring in S5.** This class exists for the post-M7
/// plug-in-adding UI session (M20+) to consume. S5 ships only the surface
/// + integration tests that drive it via the synthetic plug-in.
///
/// **macOS only.** On non-Apple platforms this Component compiles to a
/// silent placeholder (no NSViewComponent, no CALayerHost — would be the
/// Win32 / X11 path in their later sessions).
class OutOfProcessEditorView : public juce::Component,
                               private juce::Timer
{
public:
    /// Polls every `kPollMs` milliseconds for an updated CAContextID.
    /// 30 Hz matches the lower end of GUI repaint cadence and keeps the
    /// engine UI responsive to a supervisor restart re-publish without
    /// burning CPU.
    static constexpr int kPollMs = 33;

    /// Constructs a view bound to the given slot. The host is non-owning;
    /// the caller (eventually `MainComponent`) keeps it alive for the
    /// lifetime of all OutOfProcessEditorView instances.
    OutOfProcessEditorView (OutOfProcessEffectChainHost& host,
                            std::int64_t busId,
                            std::size_t slotIndex);

    ~OutOfProcessEditorView() override;

    OutOfProcessEditorView (const OutOfProcessEditorView&)            = delete;
    OutOfProcessEditorView& operator= (const OutOfProcessEditorView&) = delete;

    /// The CAContextID currently embedded, or 0 if no editor has been
    /// published yet. Exposed for the integration tests; the production
    /// UI never reads this directly.
    std::uint32_t currentContextId() const noexcept { return currentContextId_; }

    /// The slot key this view is bound to.
    std::int64_t  busId()     const noexcept { return busId_; }
    std::size_t   slotIndex() const noexcept { return slotIndex_; }

    // ---- juce::Component overrides --------------------------------------
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    /// Polls the slot's CAContextID and rewires the embedded view if it
    /// changed. Safe to call any time; idempotent.
    void refreshFromHost();

    /// Issues a Show request at the current bounds. No-op if zero-sized.
    void issueShowIfSized();

    OutOfProcessEffectChainHost& host_;
    std::int64_t                 busId_;
    std::size_t                  slotIndex_;
    bool                         shownOnce_ { false };
    std::uint32_t                currentContextId_ { 0 };

   #if JUCE_MAC
    /// JUCE wrapper around the foreign NSView we own. Holds the
    /// CALayerHost backing the embedded CAContext.
    std::unique_ptr<juce::NSViewComponent> embed_;
   #endif
};

} // namespace sirius
