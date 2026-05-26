#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ida/FileInputRegistry.h"

namespace ida
{

/// Floating, QuickTime-style player window for one file input. Pure
/// view onto FileInputRegistry state; polls fileInputTransportState
/// at 30 Hz. Closing the red X destroys the window but leaves the
/// registry's underlying FileInputSource untouched.
///
/// Lives in app/ (not ui/) because IdaUi does not link Ida::Audio.
/// app/ already links both, so the window can hold a direct reference
/// to the registry without adding new layer dependencies. (Plan said
/// ui/ — this is the documented deviation: putting FileInputRegistry
/// behind IdaUi's public link surface would force every IdaUi consumer
/// to also link Ida::Audio, which we don't want.)
class FileInputPlayerWindow : public juce::DocumentWindow,
                              private juce::Timer
{
public:
    FileInputPlayerWindow (FileInputRegistry& registry, InputId id);
    ~FileInputPlayerWindow() override;

    void closeButtonPressed() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    int  getDesktopWindowStyleFlags() const override;

    /// Public so the inner Content can forward right-clicks here (JUCE's
    /// mouseDown does NOT bubble to parents; without this forwarding the
    /// context menu would be unreachable once Task 4 drops the title bar).
    void showOpacityMenu();

    /// Toggle path shared by the right-click menu and Content's pin button.
    /// Writes the registry, calls JUCE's Component::setAlwaysOnTop, and bumps
    /// the native macOS NSWindow level to NSStatusWindowLevel so the pin
    /// survives cross-app focus changes (NSFloatingWindowLevel can be
    /// displaced by other apps' active windows on macOS).
    void setAlwaysOnTopWithNativeBump (bool onTop);

private:
    class Content;

    void timerCallback() override;          ///< 30 Hz UI refresh
    void showCustomOpacityDialog();         ///< invoked by the Custom… menu item

    FileInputRegistry& registry_;
    InputId            id_;
    std::unique_ptr<Content> content_;
};

} // namespace ida
