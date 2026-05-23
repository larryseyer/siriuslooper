#include "MainComponent.h"

#include "OTTOLookAndFeel.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace ida
{

class IDAApplication final : public juce::JUCEApplication
{
public:
    IDAApplication() = default;

    const juce::String getApplicationName() override    { return "IDA"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        // Sister-app visual parity: OTTO's LookAndFeel drives the whole UI —
        // fonts, colours, sliders, menus. Installed as the default before any
        // component (incl. MainWindow's background colour lookup) is created,
        // and owned here so it outlives every component that references it.
        lookAndFeel = std::make_unique<otto::OTTOLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<otto::OTTOLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace ida

START_JUCE_APPLICATION (ida::IDAApplication)
