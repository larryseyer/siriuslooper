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
#include "sirius/PluginDescriptor.h"

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
       #ifdef IDA_PLUGIN_HOST_PATH
        return juce::File (juce::String (IDA_PLUGIN_HOST_PATH));
       #else
        return juce::File();
       #endif
    }

    juce::File syntheticClapForTesting()
    {
       #ifdef IDA_SYNTHETIC_CLAP_PATH
        return juce::File (juce::String (IDA_SYNTHETIC_CLAP_PATH));
       #else
        return juce::File();
       #endif
    }
}

TEST_CASE ("MainComponent constructs + destructs cleanly with no editor windows",
           "[main-component-plugin-editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    {
        ida::MainComponent component;
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
    ida::MainComponent component;

    ida::PluginDescriptor descriptor;
    descriptor.format       = ida::PluginFormat::Clap;
    descriptor.uniqueId     = "com.sirius.synthetic.test";
    descriptor.name         = "Synthetic Test Plug-in";
    descriptor.manufacturer = "IDA";
    descriptor.filePath     = bundle.getFullPathName().toStdString();

    // openPluginEditor uses MainComponent::hostBinaryPath() which resolves
    // from the running binary. If the test binary lives somewhere without
    // ida_plugin_host as a sibling, hostBinaryPath() returns invalid
    // and openPluginEditor bails silently — skip in that case.
    if (! component.hostBinaryPathForTesting().existsAsFile())
        SKIP ("hostBinaryPath() not resolvable from test binary location");

    component.openPluginEditorForTesting (descriptor);
    CHECK (component.editorWindowCountForTesting() == 1);

    const auto busId = component.firstOpenBusIdForTesting();
    REQUIRE (busId >= 0);
    const auto childPid = component.childPidForTestingAtBusId (busId);
    CHECK (childPid > 0);

    component.closePluginEditorForTesting (busId);
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
    ida::MainComponent component;

    if (! component.hostBinaryPathForTesting().existsAsFile())
        SKIP ("hostBinaryPath() not resolvable from test binary location");

    ida::PluginDescriptor descriptor;
    descriptor.format       = ida::PluginFormat::Clap;
    descriptor.uniqueId     = "com.sirius.synthetic.test";
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
