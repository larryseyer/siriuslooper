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

// Prime OttoHost's TransportTracker: drive one baseline block + drain so
// the tracker has a "previous state" to diff against on the next update.
// OTTO's TransportTracker::detectAndPublishChanges short-circuits on
// hasReceivedFirstUpdate_==false (no spurious init events), so without this
// prelude the FIRST renderBlock after play()/setTempo()/stop() would be the
// tracker's baseline rather than its diff source and no listener would fire.
// In production the audio callback fires many blocks before the user touches
// the transport bar; this helper mirrors that production reality. Do NOT
// inline this into individual TEST_CASEs — duplication invites accidental
// removal during future refactors.
void primeTransport (ida::OttoHost& host, juce::MidiBuffer& midi)
{
    host.renderBlock (kBlockSize, midi);
    host.drainForTesting();
}

} // namespace

TEST_CASE ("OttoHost full pump: play → renderBlock drains, advances transport, fires listener",
           "[otto-host-pump][play]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    juce::MidiBuffer midi;
    primeTransport (host, midi);

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
    primeTransport (host, midi);

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
    primeTransport (host, midi);

    // Start playing so Stop is observable.
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

// Pointer-chain liveness pin: this TEST_CASE proves only that
// processGlobalMixer ran and the per-channel accessor returns a non-null
// pointer after renderBlock. It does NOT prove sample-level equivalence
// with OTTO standalone (that contract is preserved by the verbatim move
// in OTTO's processBlock split and pinned by OTTO's own
// [processBlock-split][transport-state-parity] test, which runs from
// OTTO's standalone CMake — currently pre-existingly broken; tracked
// separately). Future regression hunts: trust this only as far as
// "the pointer-chain is alive after pump", not as a behavioral proof
// of the audio content.
TEST_CASE ("OttoHost full pump: after play, getOttoOutputLeft(0) returns non-null",
           "[otto-host-pump][channel-accessor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    host.prepare (kSampleRate, kBlockSize);

    host.play();
    juce::MidiBuffer midi;
    host.renderBlock (kBlockSize, midi);

    REQUIRE (host.getOttoOutputLeft  (0) != nullptr);
    REQUIRE (host.getOttoOutputRight (0) != nullptr);
}
