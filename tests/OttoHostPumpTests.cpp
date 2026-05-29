// S3c — end-to-end [otto-host-pump] tests. Pin the IDA-side equivalence
// of the OTTO processBlock split: play/stop/setTempo + renderBlock must
// drive the SPSC AudioMessage queue → TransportTracker state change →
// EventBus publish → listener fan-out, and getOttoOutputLeft/Right must
// return live pointers after renderBlock so the Output Mixer can read
// per-channel OTTO audio.
//
// `juce::ScopedJuceInitialiser_GUI` is required because OttoHost owns a
// `juce::Timer` whose ctor touches the MessageManager (same pattern as
// the sibling OttoHost tests).

#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{

struct CapturingListener : public ida::IOttoTransportListener
{
    std::vector<ida::TransportSnapshot> received;

    void onOttoTransport (const ida::TransportSnapshot& s) override
    {
        received.push_back (s);
    }

    bool sawKind (ida::TransportSnapshot::Kind kind) const
    {
        for (const auto& s : received)
            if (s.kind == kind)
                return true;
        return false;
    }
};

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 256;

} // namespace

TEST_CASE ("OttoHost full pump: play → renderBlock drains, advances transport, fires listener",
           "[otto-host-pump][play]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    juce::MidiBuffer midi;

    // Prime the TransportTracker. OTTO's tracker suppresses events on the
    // first update() so it doesn't fire spurious init events; we drive one
    // baseline block so the tracker has a "previous state" to diff against.
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.play();
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    REQUIRE (listener.sawKind (ida::TransportSnapshot::Kind::Started));

    host.removeTransportListener (&listener);
}

TEST_CASE ("OttoHost full pump: setTempo → renderBlock fires BpmChanged",
           "[otto-host-pump][tempo]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    juce::MidiBuffer midi;

    // Prime the TransportTracker (see [otto-host-pump][play] comment).
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.setTempo (132.0);
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    REQUIRE (listener.sawKind (ida::TransportSnapshot::Kind::BpmChanged));

    host.removeTransportListener (&listener);
}

TEST_CASE ("OttoHost full pump: stop → renderBlock fires Stopped",
           "[otto-host-pump][stop]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    juce::MidiBuffer midi;

    // Prime the TransportTracker, then start playing so Stop is observable.
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    host.play();
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    CapturingListener listener;
    host.addTransportListener (&listener);

    host.stop();
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();

    REQUIRE (listener.sawKind (ida::TransportSnapshot::Kind::Stopped));

    host.removeTransportListener (&listener);
}

TEST_CASE ("OttoHost full pump: after play, getOttoOutputLeft(0) returns non-null",
           "[otto-host-pump][channel-accessor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    host.play();
    juce::MidiBuffer midi;
    host.renderBlock (kBlockSize, midi);

    // No kit loaded in headless tests, but processGlobalMixer should have
    // run and the per-channel accessor should expose a live pointer.
    REQUIRE (host.getOttoOutputLeft  (0) != nullptr);
    REQUIRE (host.getOttoOutputRight (0) != nullptr);
}
