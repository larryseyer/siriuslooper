#pragma once

#include <memory>

namespace ida
{

/// Owns one OTTO runtime instance (PlayerManager + TransportTracker) for
/// the lifetime of an IDA session. The header is JUCE-free and OTTO-free
/// on purpose: consumers depend only on `Ida::OttoBridge` and never see
/// `otto::` or `juce::` types. The implementation lives in OttoHost.cpp
/// and uses the pimpl pattern to hide the OTTO-side state.
///
/// M-OTTO-2 scope: ctor + dtor + prepare (the lifecycle skeleton).
/// Transport listener fan-out and audio rendering land in M-OTTO-3 +
/// M-OTTO-4 respectively (`docs/superpowers/specs/2026-05-26-otto-integration-scope-and-sequencing.md`).
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ida
