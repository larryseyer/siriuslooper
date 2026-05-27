#pragma once

#include <memory>

namespace ida
{

class IOttoTransportListener;

/// Owns one OTTO runtime instance (PlayerManager + TransportTracker) for
/// the lifetime of an IDA session. The header is JUCE-free and OTTO-free
/// on purpose: consumers depend only on `Ida::OttoBridge` and never see
/// `otto::` or `juce::` types. The implementation lives in OttoHost.cpp
/// and uses the pimpl pattern to hide the OTTO-side state.
///
/// M-OTTO-2 scope: ctor + dtor + prepare (the lifecycle skeleton).
/// M-OTTO-3 scope: transport listener fan-out (this header).
/// M-OTTO-4 scope: audio rendering through the audio thread.
/// (`docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`)
class OttoHost
{
public:
    OttoHost();
    ~OttoHost();

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ida
