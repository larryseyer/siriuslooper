// Tests for ida::OttoHost transport-listener fan-out (M-OTTO-3a). The host
// subscribes to OTTO's EventBus<TransportEvent>; transport state changes are
// translated into IDA's TransportSnapshot POD, marshalled across a lock-free
// SPSC ring, and delivered to IOttoTransportListener instances by a juce::
// Timer-driven drainer on the message thread. Tests publish directly to OTTO's
// EventBus singleton (the publish call runs the subscription handler
// synchronously, pushing into the ring), then call `drainForTesting()` to fan
// out without depending on JUCE's modal dispatch loop (the IdaTests harness
// compiles without `JUCE_MODAL_LOOPS_PERMITTED`).
//
// `juce::ScopedJuceInitialiser_GUI` is required because OttoHost owns a
// `juce::Timer` whose ctor touches the MessageManager.

#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"
#include "ida/FileInputRegistry.h"

#include <otto/common/EventBus.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using ida::IOttoTransportListener;
using ida::OttoHost;
using ida::TransportSnapshot;

namespace
{

struct RecordingListener : public IOttoTransportListener
{
    void onOttoTransport (const TransportSnapshot& snap) override
    {
        received.push_back (snap);
    }

    std::vector<TransportSnapshot> received;
};

::otto::TransportEvent makeEvent (::otto::TransportEvent::Type type,
                                  bool isPlaying,
                                  double bpm,
                                  int tsNum = 4,
                                  int tsDen = 4)
{
    ::otto::TransportEvent event;
    event.type = type;
    event.state.isPlaying = isPlaying;
    event.state.bpm = bpm;
    event.state.timeSignature.numerator = tsNum;
    event.state.timeSignature.denominator = tsDen;
    return event;
}

} // namespace

TEST_CASE ("OttoHost forwards EventBus TransportEvent::Play as TransportSnapshot::Started",
           "[otto-host-transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    RecordingListener listener;
    host.addTransportListener (&listener);

    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Play, /*isPlaying=*/true, /*bpm=*/120.0));
    host.drainForTesting();

    REQUIRE (listener.received.size() == 1);
    CHECK (listener.received[0].kind       == TransportSnapshot::Kind::Started);
    CHECK (listener.received[0].isPlaying  == true);
    CHECK (listener.received[0].bpm        == Catch::Approx (120.0));
    CHECK (listener.received[0].timeSigNum == 4);
    CHECK (listener.received[0].timeSigDen == 4);
}

TEST_CASE ("OttoHost translates each EventBus TransportEvent::Type into the matching TransportSnapshot::Kind",
           "[otto-host-transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    RecordingListener listener;
    host.addTransportListener (&listener);

    auto& bus = ::otto::EventBus::instance();
    bus.publish (makeEvent (::otto::TransportEvent::Type::Play,                 true,  140.0));
    bus.publish (makeEvent (::otto::TransportEvent::Type::Stop,                 false, 140.0));
    bus.publish (makeEvent (::otto::TransportEvent::Type::BPMChange,            false, 90.0));
    bus.publish (makeEvent (::otto::TransportEvent::Type::TimeSignatureChange,  false, 90.0, 7, 8));
    host.drainForTesting();

    REQUIRE (listener.received.size() == 4);
    CHECK (listener.received[0].kind == TransportSnapshot::Kind::Started);
    CHECK (listener.received[1].kind == TransportSnapshot::Kind::Stopped);
    CHECK (listener.received[2].kind == TransportSnapshot::Kind::BpmChanged);
    CHECK (listener.received[2].bpm  == Catch::Approx (90.0));
    CHECK (listener.received[3].kind == TransportSnapshot::Kind::TimeSigChanged);
    CHECK (listener.received[3].timeSigNum == 7);
    CHECK (listener.received[3].timeSigDen == 8);
}

TEST_CASE ("OttoHost removeTransportListener stops delivery to that listener",
           "[otto-host-transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    RecordingListener kept;
    RecordingListener removed;
    host.addTransportListener (&kept);
    host.addTransportListener (&removed);

    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Play, true, 120.0));
    host.drainForTesting();
    REQUIRE (kept.received.size()    == 1);
    REQUIRE (removed.received.size() == 1);

    host.removeTransportListener (&removed);

    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Stop, false, 120.0));
    host.drainForTesting();
    CHECK (kept.received.size()    == 2);
    CHECK (removed.received.size() == 1);  // unchanged after removal
}

TEST_CASE ("OttoHost destruction unsubscribes — late publishes do not crash and do not reach the listener",
           "[otto-host-transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    RecordingListener listener;
    {
        OttoHost host;
        host.addTransportListener (&listener);

        ::otto::EventBus::instance().publish (
            makeEvent (::otto::TransportEvent::Type::Play, true, 120.0));
        host.drainForTesting();
        REQUIRE (listener.received.size() == 1);
    }
    // host is gone — the SubscriptionHandle dtor unsubscribed before we
    // left the scope, so this publish must not reach the (now dead)
    // listener pointer. No drain needed: the subscription is gone, so
    // nothing was ever pushed into a ring.
    const auto receivedBefore = listener.received.size();
    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Stop, false, 120.0));
    CHECK (listener.received.size() == receivedBefore);
}

TEST_CASE ("OttoHost is robust to publishes with no listeners attached",
           "[otto-host-transport]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;  // no listeners

    // Publish should push into the ring; drain should pop without crashing.
    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Play, true, 100.0));
    host.drainForTesting();  // SUCCESS: no listener invoked, no crash.
}

TEST_CASE ("FileInputRegistry implements IOttoTransportListener and records last snapshot",
           "[otto-host-transport][file-input]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    ida::FileInputRegistry registry { 48000.0 };
    host.addTransportListener (&registry);

    CHECK_FALSE (registry.lastOttoTransport().has_value());

    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Play, true, 132.0, 3, 4));
    host.drainForTesting();

    REQUIRE (registry.lastOttoTransport().has_value());
    CHECK (registry.lastOttoTransport()->kind       == TransportSnapshot::Kind::Started);
    CHECK (registry.lastOttoTransport()->isPlaying  == true);
    CHECK (registry.lastOttoTransport()->bpm        == Catch::Approx (132.0));
    CHECK (registry.lastOttoTransport()->timeSigNum == 3);
    CHECK (registry.lastOttoTransport()->timeSigDen == 4);

    // Subsequent event overwrites: registry stores only the most recent.
    ::otto::EventBus::instance().publish (
        makeEvent (::otto::TransportEvent::Type::Stop, false, 132.0, 3, 4));
    host.drainForTesting();
    REQUIRE (registry.lastOttoTransport().has_value());
    CHECK (registry.lastOttoTransport()->kind      == TransportSnapshot::Kind::Stopped);
    CHECK (registry.lastOttoTransport()->isPlaying == false);
}
