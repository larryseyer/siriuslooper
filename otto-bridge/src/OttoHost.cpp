#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"

#include "ida/LockFreeSpscQueue.h"
#include "ida/MasterMeter.h"
#include "ida/MasterSpectrum.h"

#include <otto/common/AudioMessage.h>
#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/transport/TransportTracker.h>

#include <PluginProcessor.h>   // OTTOProcessor — the juce::AudioProcessor we embed

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
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
        , processor     (std::make_unique<OTTOProcessor>())
    {
        // S2: signal the editor that it is embedded in IDA (a JUCE Standalone
        // host that itself owns the transport bar + routing UI). Set BEFORE
        // any createEditor() call — OTTOEditor reads the flag at construction.
        processor->setEmbeddedInHost (true);

        // Subscribe on the message thread (ctor invariant). The handler
        // captures `this` and runs on whatever thread `EventBus::publish`
        // is invoked from — today that is OTTO's audio thread, called
        // from `OTTOProcessor::processBlock` (which drives
        // TransportTracker::update internally). The handler therefore
        // obeys audio-thread invariants: no allocation, no locks, no I/O,
        // no throw.
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
        // while we tear down. Member destruction then runs in reverse
        // declaration order: subscription (LAST-declared) unsubscribes from
        // the singleton bus, serialising against any in-flight publish;
        // then processor is torn down; then everything else.
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

    // OTTOProcessor IS a juce::AudioProcessor — prepareToPlay drives OTTO's
    // full pipeline (Conductor + Pattern engine + MIDI dispatch + sampler
    // voices + internal FX + GlobalMixer + internal TransportTracker). S1
    // embeds the processor so S2 (OttoPane via createEditor()), S3 (transport
    // bar via transportTracker accessor) and S4 (preset state) can consume
    // it. renderBlock itself still drives audio via
    // playerManager.processGlobalMixer() — that path populates GlobalMixer's
    // per-channel output pointers regardless of transport state, which is
    // what IDA's 32-output accessors read. OTTOProcessor::processBlock would
    // gate routing on conductor_.isPlaying() and leave the per-channel
    // pointers nullptr when the transport is stopped (only relevant for
    // S3's future transport drive); for S1 it would be a regression
    // against the [otto-host-render] baseline.
    std::unique_ptr<OTTOProcessor> processor;
    bool                           prepared { false };

    LockFreeSpscQueue<TransportSnapshot> transportRing;
    std::vector<IOttoTransportListener*> listeners;       // message-thread only

    // S3a T5: non-owning references to the master-mix-point publishers.
    // Set once via setMasterPublishers; null until then. Reads are
    // protected by the null check in each snapshot accessor.
    const MasterMeter*    masterMeter    { nullptr };
    const MasterSpectrum* masterSpectrum { nullptr };

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
    // Drive OTTO's AudioProcessor prepareToPlay — propagates internally to
    // PlayerManager::prepare AND to every other subsystem OTTOProcessor owns
    // (Conductor, Pattern engine, internal FX, TransportTracker). S2+ will
    // additionally drive processBlock for transport advancement.
    impl_->processor->prepareToPlay (sampleRate, maxBlockSize);
    impl_->prepared = true;
}

bool OttoHost::isPrepared() const noexcept
{
    return impl_->prepared;
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
    if (! impl_->prepared || numSamples <= 0)
        return;

    // Drive GlobalMixer through the embedded OTTOProcessor's PlayerManager.
    // processGlobalMixer runs channels + sends + buses + meter taps in
    // sequence and is RT-safe per OTTO's CLAUDE.md. After it returns, the
    // per-channel / per-FX-return / per-player-bus accessors on GlobalMixer
    // return live pointers into OTTO's pre-master mixer buffers. (See Impl
    // struct comment for why we do not drive processor->processBlock here.)
    impl_->processor->getPlayerManager().processGlobalMixer (numSamples);
}

const float* OttoHost::getOttoOutputLeft (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->prepared)
        return nullptr;

    const auto& mixer = impl_->processor->getPlayerManager().getGlobalMixer();
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
    if (! impl_->prepared)
        return nullptr;

    const auto& mixer = impl_->processor->getPlayerManager().getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputRight (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputRight (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputRight (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}

juce::AudioProcessor& OttoHost::getProcessor() noexcept
{
    return *impl_->processor;
}

// -----------------------------------------------------------------------------
// S3a T5 — transport controls
// -----------------------------------------------------------------------------
//
// Mirrors `OTTOEditor::onPlayPauseClicked` / `onStopClicked` / `onTempoChanged`
// in `external/OTTO/src/otto-plugin/PluginEditor.cpp` — the editor-side path
// posts an `otto::AudioMessage` via `OTTOProcessor::sendToAudioThread`. We do
// the same here, minus the editor-only FillBag/timeline re-seed logic which is
// an editor concern. Pause is not used by IDA; play/stop are the two states
// the persistent transport bar drives.

void OttoHost::play()
{
    ::otto::AudioMessage msg;
    msg.type     = ::otto::AudioMessageType::TransportControl;
    msg.intValue = static_cast<int> (::otto::TransportCommand::Play);
    impl_->processor->sendToAudioThread (msg);
}

void OttoHost::stop()
{
    ::otto::AudioMessage msg;
    msg.type     = ::otto::AudioMessageType::TransportControl;
    msg.intValue = static_cast<int> (::otto::TransportCommand::Stop);
    impl_->processor->sendToAudioThread (msg);
}

void OttoHost::setTempo (double bpm)
{
    ::otto::AudioMessage msg;
    msg.type       = ::otto::AudioMessageType::TempoChange;
    msg.floatValue = static_cast<float> (bpm);
    impl_->processor->sendToAudioThread (msg);
}

void OttoHost::tapTempo()
{
    // OTTO does not yet have an audio-thread tap-tempo path —
    // `OTTOEditor::tapTempo()` is itself a stub with the same comment. The
    // IDA-side TransportBar (T6) still calls this so the wiring is in place
    // when OTTO adds support.
}

// -----------------------------------------------------------------------------
// S3a T5 — master-output snapshot accessors
// -----------------------------------------------------------------------------

OttoHost::MasterSnapshot OttoHost::snapshotMaster() const noexcept
{
    if (impl_->masterMeter == nullptr)
        return { -100.0f, -100.0f, -100.0f, -100.0f };

    const auto s = impl_->masterMeter->snapshot();
    return { s.leftDb, s.rightDb, s.peakDb, s.lufs };
}

int OttoHost::spectrumBinCount() const noexcept
{
    return impl_->masterSpectrum != nullptr ? impl_->masterSpectrum->numBins() : 0;
}

float OttoHost::spectrumBinDb (int bin) const noexcept
{
    return impl_->masterSpectrum != nullptr ? impl_->masterSpectrum->binDb (bin) : -100.0f;
}

void OttoHost::setMasterPublishers (const MasterMeter& meter,
                                    const MasterSpectrum& spec) noexcept
{
    impl_->masterMeter    = &meter;
    impl_->masterSpectrum = &spec;
}

} // namespace ida
