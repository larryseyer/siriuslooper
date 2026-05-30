#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"
#include "ida/TransportPlayhead.h"

#include "ida/LockFreeSpscQueue.h"
#include "ida/MasterMeter.h"
#include "ida/MasterSpectrum.h"

#include <otto/common/AudioMessage.h>
#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/paths/AssetsRoot.h>
#include <otto/transport/TransportTracker.h>

#include <PluginProcessor.h>   // OTTOProcessor — the juce::AudioProcessor we embed

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
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
        , processor     (
            // S3b T13 — set OTTO's AssetsRoot override BEFORE OTTOProcessor's
            // ctor runs. The IIFE sequences setOverride strictly before
            // make_unique<OTTOProcessor>(): the lambda body runs as part of
            // the `processor` member-init expression, and the lambda's return
            // value is what initializes `processor`. OTTO's per-platform path
            // ladders (SamplerPresetLoader::findSamplerFolder,
            // IRPresetLoader::findAllIRFolders, PresetPaths::getRoot(Factory))
            // consult AssetsRoot first; without this override they fall back
            // to OTTO bundle paths that do not exist when running inside IDA.app.
            []{
                otto::paths::AssetsRoot::instance().setOverride (juce::File { IDA_OTTO_ASSETS_DIR });
                return std::make_unique<OTTOProcessor>();
            }())
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
    // voices + internal FX + GlobalMixer + internal TransportTracker).
    // renderBlock drives the per-block audio pump via OTTO's S3c split:
    // processBlockBeforeRouting + processGlobalMixer + processBlockAfterRouting.
    // See docs/superpowers/specs/2026-05-28-otto-audio-pump-design.md.
    std::unique_ptr<OTTOProcessor> processor;
    bool                           prepared { false };
    double                         preparedSampleRate { 0.0 };

    // IDA-embedding selective-bus mask (see OttoHost::setActivePlayerBusMask).
    // Default all-consumed → no behavior change. Stored here so it survives
    // and is re-applied across prepare() (prepareToPlay does not reset the
    // GlobalMixer's own default-all-set mask, but re-applying keeps IDA's
    // chosen mask authoritative after any re-prepare).
    std::uint32_t                  activePlayerBusMask { 0xFFFFFFFFu };

    LockFreeSpscQueue<TransportSnapshot> transportRing;
    std::vector<IOttoTransportListener*> listeners;       // message-thread only

    // S3a T5: non-owning references to the master-mix-point publishers.
    // Set once via setMasterPublishers; null until then. Reads are
    // protected by the null check in each snapshot accessor.
    const MasterMeter*    masterMeter    { nullptr };
    const MasterSpectrum* masterSpectrum { nullptr };

    // T0b — transport playhead. playedSamples_ is audio-thread private
    // (no sharing); playheadSeconds_ and playheadPlaying_ are published
    // via release/acquire atomics for any-thread snapshot reads.
    std::int64_t        playedSamples_   { 0 };       // audio-thread private
    std::atomic<double> playheadSeconds_ { 0.0 };     // published
    std::atomic<bool>   playheadPlaying_ { false };   // published

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
    impl_->prepared           = true;
    impl_->preparedSampleRate = sampleRate;
    impl_->playedSamples_     = 0;

    // Re-assert IDA's selective-bus mask after (re-)prepare so a strip set
    // chosen before this prepare stays authoritative.
    impl_->processor->getPlayerManager().getGlobalMixer()
        .setActiveBusMask (impl_->activePlayerBusMask);
}

void OttoHost::setActivePlayerBusMask (std::uint32_t categoryMask) noexcept
{
    impl_->activePlayerBusMask = categoryMask;
    if (impl_->prepared)
        impl_->processor->getPlayerManager().getGlobalMixer()
            .setActiveBusMask (categoryMask);
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

void OttoHost::renderBlock (int numSamples,
                            juce::MidiBuffer& midiMessages) noexcept
{
    if (! impl_->prepared || numSamples <= 0)
        return;

    // S3c — IDA's OTTO audio pump. Drives OTTO's processBlock housekeeping
    // prefix + per-channel sum + housekeeping suffix, skipping OTTO's master
    // mixdown path (which competes with IDA's Output Mixer). See
    // docs/superpowers/specs/2026-05-28-otto-audio-pump-design.md.
    juce::ScopedNoDenormals noDenormals;

    auto& proc = *impl_->processor;

    // Half A prefix: drain Play/Stop/TempoChange AudioMessages, update
    // transport tracker (which publishes TransportEvent → the EventBus
    // subscription above marshals into the SPSC ring → drainer fans out
    // to listeners), pin engines, dispatch host MIDI into sfizz, advance
    // the conductor + song timeline.
    proc.processBlockBeforeRouting (midiMessages, numSamples);

    // Per-channel / per-FX-return / per-player-bus sum. Populates the
    // GlobalMixer accessors IDA's Output Mixer reads via
    // getOttoOutputLeft/Right. Replaces OTTO's processBlock master path
    // (outputRouter_.routeAudio + de-click + spectrum + clear) entirely
    // — IDA owns the master.
    proc.getPlayerManager().processGlobalMixer (numSamples);

    // Half A suffix: totalSamplePosition advance + per-player fillMode
    // sync to pluginState_ (for UI display).
    proc.processBlockAfterRouting (numSamples);

    // T0b — advance the playhead clock. Pure arithmetic + two lock-free
    // atomic stores; RT-safe (no alloc, no lock, no I/O).
    const bool playing = proc.getConductor().isPlaying();
    impl_->playedSamples_ = advancePlayedSamples (impl_->playedSamples_, numSamples, playing);
    impl_->playheadSeconds_.store (playheadSeconds (impl_->playedSamples_, impl_->preparedSampleRate),
                                   std::memory_order_release);
    impl_->playheadPlaying_.store (playing, std::memory_order_release);
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

TransportPlayhead OttoHost::snapshotPlayhead() const noexcept
{
    return { impl_->playheadSeconds_.load (std::memory_order_acquire),
             impl_->playheadPlaying_.load (std::memory_order_acquire) };
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

double OttoHost::getPreparedSampleRate() const noexcept
{
    return impl_->preparedSampleRate;
}

} // namespace ida
