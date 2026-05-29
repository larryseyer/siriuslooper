// S6 T4 — end-to-end OTTO strip persistence + rebind. Modeled on
// OttoHostRenderTests.cpp (prepared OttoHost + freshly-constructed OutputMixer,
// no GUI). Exercises ida::app::rebindOttoChannelsAfterImport directly so the
// MainComponent post-import logic is verifiable without instantiating MainComponent.

#include "OttoStripRebind.h"
#include "ida/OttoHost.h"
#include "ida/OutputMixer.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 64;

    // OTTO's per-output buffer pointers become non-null only after the first
    // `renderBlock` call (matches the pattern in OttoHostRenderTests.cpp).
    void primeOttoOutputs (ida::OttoHost& host)
    {
        juce::MidiBuffer midi;
        host.renderBlock (kBlockSize, midi);
    }
}

TEST_CASE ("rebindOttoChannelsAfterImport binds OTTO output pointers + populates the map",
           "[otto-strip][persistence][end-to-end]")
{
    using namespace ida;

    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kSampleRate, kBlockSize);
    primeOttoOutputs (host);

    OutputMixer mix;
    const auto chPhrase = mix.addChannel (SignalType::Audio);
    const auto chOtto0  = mix.addChannel (SignalType::Audio);
    const auto chOtto7  = mix.addChannel (SignalType::Audio);

    mix.setOttoSource (chPhrase, kOttoSourcePhraseChannel);
    mix.setOttoSource (chOtto0,  0);
    mix.setOttoSource (chOtto7,  7);

    SECTION ("rebind binds the OTTO output L/R pointers + populates the map")
    {
        std::unordered_map<int, OutputChannelId> ottoMap;
        ida::app::rebindOttoChannelsAfterImport (mix, host, ottoMap);

        REQUIRE (ottoMap.size() == 2);
        REQUIRE (ottoMap.at (0) == chOtto0);
        REQUIRE (ottoMap.at (7) == chOtto7);

        // The phrase channel is left alone — no entry in the map.
        REQUIRE (ottoMap.count (-1) == 0);
    }

    SECTION ("rebind is idempotent — second call is a no-op")
    {
        std::unordered_map<int, OutputChannelId> ottoMap;
        ida::app::rebindOttoChannelsAfterImport (mix, host, ottoMap);
        const auto firstSize = ottoMap.size();

        ida::app::rebindOttoChannelsAfterImport (mix, host, ottoMap);
        REQUIRE (ottoMap.size() == firstSize);
        REQUIRE (ottoMap.at (0) == chOtto0);
        REQUIRE (ottoMap.at (7) == chOtto7);
    }
}

TEST_CASE ("OTTO strip HardwareOutput route survives export + import + rebind",
           "[otto-strip][persistence][end-to-end]")
{
    using namespace ida;

    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kSampleRate, kBlockSize);
    primeOttoOutputs (host);

    OutputMixer mix;
    const auto chOtto5 = mix.addChannel (SignalType::Audio);
    mix.setOttoSource (chOtto5, 5);
    mix.setChannelMainOutToHardwareOutput (chOtto5, /*pairIndex*/ 1);

    const auto state = mix.exportGraphState();

    OutputMixer fresh;
    fresh.importGraphState (state);

    std::unordered_map<int, OutputChannelId> ottoMap;
    ida::app::rebindOttoChannelsAfterImport (fresh, host, ottoMap);

    REQUIRE (ottoMap.count (5) == 1);
    const auto rebound = ottoMap.at (5);
    REQUIRE (fresh.channelOttoSource (rebound) == 5);
    REQUIRE (fresh.channelMainOut (rebound) == OutputMixer::MainOutDest::HardwareOutput);
    REQUIRE (fresh.channelMainOutHardwareOutPair (rebound) == 1);
}

TEST_CASE ("OTTO strip Bus route survives export + import + rebind",
           "[otto-strip][persistence][end-to-end]")
{
    using namespace ida;

    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kSampleRate, kBlockSize);
    primeOttoOutputs (host);

    OutputMixer mix;
    const auto auxBus = mix.addBus ({ /*channelCount*/ 2, "Aux", BusKind::Bus });
    const auto chOtto12 = mix.addChannel (SignalType::Audio);
    mix.setOttoSource (chOtto12, 12);
    mix.routeChannelToBus (chOtto12, auxBus, 1.0f);  // radio: zeros master, sets mainOut=Bus(aux)

    const auto state = mix.exportGraphState();

    OutputMixer fresh;
    fresh.importGraphState (state);

    std::unordered_map<int, OutputChannelId> ottoMap;
    ida::app::rebindOttoChannelsAfterImport (fresh, host, ottoMap);

    REQUIRE (ottoMap.count (12) == 1);
    const auto rebound = ottoMap.at (12);
    REQUIRE (fresh.channelOttoSource (rebound) == 12);
    REQUIRE (fresh.channelMainOut (rebound) == OutputMixer::MainOutDest::Bus);
    REQUIRE (fresh.channelMainOutBus (rebound) == auxBus);
}
