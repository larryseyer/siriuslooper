#pragma once

#include "ida/IOttoRenderSource.h"

#include <memory>

namespace juce { class AudioProcessor; }

namespace ida
{

class IOttoTransportListener;
class MasterMeter;
class MasterSpectrum;

/// Owns one OTTO runtime instance (PlayerManager + TransportTracker) for
/// the lifetime of an IDA session. The header is JUCE-free and OTTO-free
/// on purpose: consumers depend only on `Ida::OttoBridge` and never see
/// `otto::` or `juce::` types. The implementation lives in OttoHost.cpp
/// and uses the pimpl pattern to hide the OTTO-side state.
///
/// M-OTTO-2 scope: ctor + dtor + prepare (the lifecycle skeleton).
/// M-OTTO-3 scope: transport listener fan-out (this header).
/// M-OTTO-4 scope: audio rendering through the audio thread (renderBlock +
/// the IOttoRenderSource port the AudioCallback drives once per buffer).
/// (`docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`)
class OttoHost : public IOttoRenderSource
{
public:
    OttoHost();
    ~OttoHost() override;

    OttoHost (const OttoHost&)            = delete;
    OttoHost& operator= (const OttoHost&) = delete;
    OttoHost (OttoHost&&)                 = delete;
    OttoHost& operator= (OttoHost&&)      = delete;

    /// Forwarded to OTTO's `PlayerManager::prepare`. Allocates per-block
    /// buffers + per-player synthesis state. Message-thread only.
    void prepare (double sampleRate, int maxBlockSize);

    /// Mirrors OTTO's `PlayerManager::isPrepared`. False until `prepare`
    /// is called at least once.
    bool isPrepared() const noexcept;

    /// Register a message-thread listener for OTTO transport changes
    /// (play/stop/bpm/time-signature). Safe to call before or after
    /// `prepare`. The listener pointer must outlive its registration —
    /// either remove it explicitly via `removeTransportListener` before
    /// destruction, or rely on member-declaration order so the
    /// `OttoHost` is destroyed first (its dtor stops the drain timer
    /// and unsubscribes from OTTO's bus before the listener vector
    /// falls out of scope, so a listener declared *after* the OttoHost
    /// member is safe without explicit removal). Message-thread only.
    void addTransportListener (IOttoTransportListener* listener);

    /// Unregister a previously-added listener. No-op if the listener
    /// was never added. Message-thread only.
    void removeTransportListener (IOttoTransportListener* listener);

    /// Test-only: synchronously drain the audio→message SPSC ring and
    /// fan out to listeners. Production code never calls this — the
    /// drainer Timer running on the message thread is the authoritative
    /// path. Tests that don't have a running message loop (the IdaTests
    /// harness compiles without `JUCE_MODAL_LOOPS_PERMITTED`) can use
    /// this to verify wiring end-to-end after publishing events.
    void drainForTesting();

    // -------------------------------------------------------------------------
    // M-OTTO-4 — 32 per-output stereo pairs
    //
    // OTTO's GlobalMixer exposes 32 stereo output pairs as the canonical
    // multi-out surface (24 instrument channels + 4 FX returns + 4 player
    // buses). The constant + range layout below mirror OTTO's compile-time
    // invariant (`GlobalMixer.h`'s static_assert that the canonical category
    // counts sum to 32 SFZ outputs). Public surface stays OTTO-type-free:
    // index in, raw `const float*` out.
    // -------------------------------------------------------------------------

    /// Total stereo output pairs OTTO exposes.
    static constexpr int kNumOttoOutputs = 32;

    /// `[kOttoChannelRangeBegin, kOttoFxReturnRangeBegin)` → 24 instrument
    /// channels (post-channel-EQ/CMP, pre-send, pre-bus). Indices 0..15 are
    /// Drum slots; 16..19 Percs; 20..21 Shakers; 22..23 Hands.
    static constexpr int kOttoChannelRangeBegin   = 0;

    /// `[kOttoFxReturnRangeBegin, kOttoPlayerBusRangeBegin)` → 4 shared FX
    /// returns (3 reverbs + 1 delay), post-FX, pre-pair-assignment.
    static constexpr int kOttoFxReturnRangeBegin  = 24;

    /// `[kOttoPlayerBusRangeBegin, kNumOttoOutputs)` → 4 per-mode sub-buses
    /// (Drums=28, Percs=29, Shakers=30, Hands=31), post-bus-EQ/CMP /
    /// post-bus-fader/pan, pre-master-bus. Most natural per-player taps.
    static constexpr int kOttoPlayerBusRangeBegin = 28;

    /// Returns the embedded `OTTOProcessor` typed as `juce::AudioProcessor&`
    /// so the public header stays OTTO-type-free. Intended caller is
    /// `ida::OttoPane`, which calls `createEditor()` on the result to
    /// host OTTO's UI as a top-level IDA tab. Message-thread only;
    /// pointer-stable for the lifetime of this `OttoHost` instance.
    juce::AudioProcessor& getProcessor() noexcept;

    /// Run one audio block of OTTO's mixer pipeline: pump all 4 players
    /// through GlobalMixer and populate the 32 per-output stereo pair
    /// buffers behind `getOttoOutputLeft/Right`. Audio-thread only.
    /// RT-safe: wraps OTTO's `PlayerManager::processGlobalMixer`, which
    /// OTTO's CLAUDE.md guarantees as alloc/lock/log-free. Must be called
    /// BEFORE any same-block read via the accessors below. A call before
    /// `prepare()` is a no-op. Satisfies the `IOttoRenderSource` port the
    /// AudioCallback drives once per buffer.
    void renderBlock (int numSamples) noexcept override;

    /// Pointer into OTTO's per-output left-channel buffer for the most
    /// recent `renderBlock`. Valid until the next `renderBlock` (or
    /// `prepare()`) — the underlying buffer is owned by OTTO's
    /// GlobalMixer and overwritten each block. Returns `nullptr` if
    /// `ottoOutputIndex` is out of `[0, kNumOttoOutputs)` or OTTO isn't
    /// prepared. Audio-thread only.
    const float* getOttoOutputLeft  (int ottoOutputIndex) const noexcept;
    const float* getOttoOutputRight (int ottoOutputIndex) const noexcept;

    // -------------------------------------------------------------------------
    // S3a T5 — transport controls + master-output snapshot accessors
    // -------------------------------------------------------------------------

    /// Transport controls — message-thread only. Forward to the embedded
    /// OTTOProcessor's existing audio-message path (see
    /// `OTTOEditor::onPlayPauseClicked` / `onStopClicked` / `onTempoChanged`
    /// in `external/OTTO/src/otto-plugin/PluginEditor.cpp`). Used by
    /// `ida::TransportBarHost` to drive OTTO from IDA's persistent
    /// transport bar.
    void play();
    void stop();
    void setTempo (double bpm);

    /// Currently a no-op: OTTO does not yet have an audio-thread tap-tempo
    /// path (`OTTOEditor::tapTempo()` is itself a stub with the same
    /// comment). The TransportBar (T6) will still call this so the
    /// wiring is in place when OTTO adds support.
    void tapTempo();

    /// Master-output meter snapshot. Any-thread (atomic load). Mirrors
    /// `ida::MasterMeter::Snapshot` field-for-field so the public surface
    /// stays JUCE-free / engine-header-free for consumers that only see
    /// OttoHost. Returns the sentinel `{-100,-100,-100,-100}` until
    /// `setMasterPublishers` has been called.
    struct MasterSnapshot { float leftDb; float rightDb; float peakDb; float lufs; };
    MasterSnapshot snapshotMaster() const noexcept;

    /// Master-spectrum bin. Any-thread (atomic load). Forwards to the
    /// `ida::MasterSpectrum` publisher wired in at the master mix point.
    /// Returns 0 / -100 dB until `setMasterPublishers` has been called.
    int   spectrumBinCount() const noexcept;
    float spectrumBinDb (int bin) const noexcept;

    /// Wire the master-mix-point publishers into the host so the snapshot
    /// accessors above can read them. Called once at MainComponent
    /// construction time after the publishers are attached to the master
    /// mix point. The references must outlive this OttoHost. Message-thread
    /// only; the stored pointers are read via atomic-load APIs on the
    /// publishers themselves.
    void setMasterPublishers (const ida::MasterMeter& meter,
                              const ida::MasterSpectrum& spec) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ida
