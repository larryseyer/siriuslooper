#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"

#include "ida/LockFreeSpscQueue.h"

#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/transport/TransportTracker.h>

#include <juce_events/juce_events.h>

#include <algorithm>
#include <functional>
#include <vector>

namespace ida
{

namespace
{

/// Per-OttoHost capacity of the audio→message-thread snapshot ring. OTTO
/// publishes at most one TransportEvent per state change per audio block;
/// a 64-slot ring absorbs ~1.3 s of worst-case 50 Hz BPM-automation bursts
/// at the 30 Hz drain cadence. The drainer reads on every timer tick so
/// in practice 4-8 slots would suffice; the headroom is cheap (POD of ~24
/// bytes) and tolerates short message-thread stalls without dropping.
constexpr std::size_t kTransportRingCapacity = 64;

/// Message-thread drain cadence. 30 Hz matches IDA's existing UI-poll
/// timers (NotificationBus drainer, file-input transport-state poll).
constexpr int kDrainTimerHz = 30;

TransportSnapshot::Kind translateKind (::otto::TransportEvent::Type t) noexcept
{
    switch (t)
    {
        case ::otto::TransportEvent::Type::Play:                return TransportSnapshot::Kind::Started;
        case ::otto::TransportEvent::Type::Stop:                return TransportSnapshot::Kind::Stopped;
        case ::otto::TransportEvent::Type::BPMChange:           return TransportSnapshot::Kind::BpmChanged;
        case ::otto::TransportEvent::Type::TimeSignatureChange: return TransportSnapshot::Kind::TimeSigChanged;
    }
    return TransportSnapshot::Kind::Stopped;
}

} // namespace

struct OttoHost::Impl : public juce::Timer
{
    Impl()
        : transportRing (kTransportRingCapacity)
    {
        // Subscribe on the message thread (ctor invariant). The handler
        // captures `this` and runs on whatever thread `EventBus::publish`
        // is invoked from — today that is OTTO's audio thread, called
        // from `TransportTracker::update()` inside `processBlock`. The
        // handler therefore obeys audio-thread invariants: no allocation,
        // no locks, no I/O, no throw.
        //
        // (Note: OTTO's `EventBus::publish` itself currently locks a mutex
        // and allocates a vector to copy the subscriber list before
        // invoking handlers — that is an OTTO-side audio-thread invariant
        // violation, surfaced via the cross-project inbox. IDA's own
        // handler stays RT-clean either way.)
        subscription = ::otto::EventBus::instance().subscribe<::otto::TransportEvent> (
            [this] (const ::otto::TransportEvent& event) noexcept
            {
                TransportSnapshot snap;
                snap.kind       = translateKind (event.type);
                snap.bpm        = event.state.bpm;
                snap.isPlaying  = event.state.isPlaying;
                snap.timeSigNum = event.state.timeSignature.numerator;
                snap.timeSigDen = event.state.timeSignature.denominator;

                // Drop on overflow rather than blocking — the SPSC contract
                // forbids the producer from waiting. A dropped snapshot is
                // recoverable by the next publish (transport state is
                // level-triggered: the next Play/Stop/BpmChange will deliver
                // current truth). 64-slot headroom makes this practically
                // unreachable.
                (void) transportRing.push (snap);
            });

        startTimerHz (kDrainTimerHz);
    }

    ~Impl() override
    {
        // Stop the drain timer first so no further listener callbacks fire
        // while we tear down. The SubscriptionHandle dtor (below, via
        // member-declaration order) unsubscribes from OTTO's bus, which
        // also blocks until any in-flight publish returns. After that the
        // listeners vector and ring are safe to drop.
        stopTimer();
    }

    void timerCallback() override
    {
        drain();
    }

    void drain()
    {
        TransportSnapshot snap;
        while (transportRing.pop (snap))
        {
            // Listeners can be added/removed from inside a callback; iterate
            // over a local copy of the pointer list so a removal during fan-
            // out doesn't invalidate our iterator. The copy is cheap (raw
            // pointers); allocation here is acceptable — we're on the
            // message thread, not the audio thread.
            const auto snapshot = listeners;
            for (auto* listener : snapshot)
                if (listener != nullptr)
                    listener->onOttoTransport (snap);
        }
    }

    ::otto::manager::PlayerManager playerManager;
    ::otto::TransportTracker       transportTracker;

    LockFreeSpscQueue<TransportSnapshot> transportRing;
    std::vector<IOttoTransportListener*> listeners;       // message-thread only

    // Declared LAST so it is destroyed FIRST. The SubscriptionHandle dtor
    // unsubscribes from the singleton bus, which serialises against any
    // in-flight `publish` — once it returns, no more callbacks can fire.
    // (juce::Timer base is already stopped at this point.)
    ::otto::SubscriptionHandle subscription;
};

OttoHost::OttoHost()
    : impl_ (std::make_unique<Impl>())
{
}

OttoHost::~OttoHost() = default;

void OttoHost::prepare (double sampleRate, int maxBlockSize)
{
    impl_->playerManager.prepare (sampleRate, maxBlockSize);
}

bool OttoHost::isPrepared() const noexcept
{
    return impl_->playerManager.isPrepared();
}

void OttoHost::addTransportListener (IOttoTransportListener* listener)
{
    if (listener == nullptr)
        return;

    auto& v = impl_->listeners;
    if (std::find (v.begin(), v.end(), listener) == v.end())
        v.push_back (listener);
}

void OttoHost::removeTransportListener (IOttoTransportListener* listener)
{
    if (listener == nullptr)
        return;

    auto& v = impl_->listeners;
    v.erase (std::remove (v.begin(), v.end(), listener), v.end());
}

void OttoHost::drainForTesting()
{
    impl_->drain();
}

void OttoHost::renderBlock (int numSamples) noexcept
{
    if (! impl_->playerManager.isPrepared() || numSamples <= 0)
        return;

    // OTTO's processGlobalMixer runs channels + sends + buses + meter taps in
    // sequence and is RT-safe per OTTO's CLAUDE.md. After it returns, the
    // per-channel / per-FX-return / per-player-bus accessors on GlobalMixer
    // return live pointers into OTTO's pre-master mixer buffers.
    impl_->playerManager.processGlobalMixer (numSamples);
}

const float* OttoHost::getOttoOutputLeft (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->playerManager.isPrepared())
        return nullptr;

    const auto& mixer = impl_->playerManager.getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputLeft (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputLeft (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputLeft (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}

const float* OttoHost::getOttoOutputRight (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->playerManager.isPrepared())
        return nullptr;

    const auto& mixer = impl_->playerManager.getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputRight (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputRight (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputRight (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}

} // namespace ida
