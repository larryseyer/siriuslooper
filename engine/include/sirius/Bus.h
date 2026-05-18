#pragma once

#include "sirius/Channel.h"
#include "sirius/EffectChain.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace sirius
{

/// Small POD describing the static shape of a Bus. Configured set-once on
/// the message thread before the audio thread ever reads the owning Bus;
/// mutating these fields after the audio thread has been started is a
/// threading-contract violation (see continue.md M4 constraint #6,
/// inherited by M5).
///
/// `channelCount` is the bus's working width — `1` for mono, `2` for stereo
/// (the M5 default). M5 does not specify surround buses; values > 2 are
/// allowed structurally but only the first two are touched by the M5 audio
/// thread DSP. `name` is for diagnostics / the operator-facing bus picker
/// only — never read on the audio thread.
struct BusConfig
{
    int         channelCount { 2 };
    std::string name;
};

/// Session-level effect bus per V3 Step 7 / V7 alignment plan M5
/// (docs/superpowers/plans/2026-05-17-v7-alignment.md lines 384-388). A Bus
/// is a destination for per-channel sends from the OutputMixer; the bus
/// applies its EffectChain (M7 turns this into real plugin invocation —
/// M5 holds the chain config-only) and contributes its mix into the master
/// bus, which itself is a Bus (`BusId{0}`) so the same code path applies
/// to both aux buses and the master.
///
/// Audio-thread invariants (docs/RT_SAFETY_CONTRACT.md §6, row added in
/// M5 Session 2, real body in M5 Session 3):
///   - `process(...)` is `noexcept`, allocation-free, lock-free, I/O-free.
///   - `mixBuffer_` is pre-allocated in the constructor (sized to
///     `kMaxBusMixSamples * kMaxBusChannelsHard`) and only indexed —
///     never resized — on the audio thread. Matches the
///     `InputMixer::processingScratch_` pattern from M5 Session 1.
///     `mutable` because `mixBufferChannel(int) const noexcept` returns a
///     writable pointer for the const audio-thread caller
///     (`OutputMixer::renderBuffer` is `const`, so the `Bus&` it holds is
///     also const — but the mix scratch IS implementation detail the
///     audio thread owns end-to-end). The contract: callers WRITE to
///     `mixBuffer_` to accumulate sends, `Bus::process` reads + zeros it.
///   - `effectChain_` is set-once on the message thread via
///     `setEffectChain`; the audio thread reads through `const` reference
///     and never mutates. M5 holds the chain config-only — actual plugin
///     dispatch lands in M7.
///   - `id_` and `config_` are set in the constructor and never reassigned.
class Bus
{
public:
    /// Soft per-buffer ceiling for the audio-thread mix scratch. Matches
    /// the InputMixer scratch ceiling so the same audio device buffer-size
    /// envelope applies (typical: 64..2048 samples).
    static constexpr std::size_t kMaxBusMixSamples = 8192;

    /// Hard width ceiling for the bus's pre-allocated mix scratch — M5 caps
    /// at stereo. Channel counts above this are clamped at construction
    /// (the structural field on `BusConfig` may still report the larger
    /// value, but the audio-thread mix scratch only ever indexes up to two
    /// channels). When surround buses become a real surface, bump this
    /// and re-audit RT_SAFETY_CONTRACT §6.
    static constexpr int kMaxBusChannelsHard = 2;

    Bus (BusId id, BusConfig config);

    BusId           id()      const noexcept { return id_; }
    const BusConfig& config() const noexcept { return config_; }

    /// Message-thread setter — copies the chain in. Set-once before the
    /// audio thread starts; mutating after start is a threading-contract
    /// violation (see class doc + continue.md constraint #6).
    void setEffectChain (EffectChain chain) { effectChain_ = std::move (chain); }

    const EffectChain& effectChain() const noexcept { return effectChain_; }

    /// Audio-thread mix entry point. M5 Session 3 body: for each active
    /// channel, additively write `mixBuffer_[c]` content into `output[c]`,
    /// then zero `mixBuffer_` so the next buffer starts fresh. EffectChain
    /// is HELD but NOT invoked in M5 — actual plugin dispatch lands in M7
    /// (V7 alignment plan M5 line 387: "EQ/dynamics stubs in M5"). Callers
    /// (typically `OutputMixer::renderBuffer`) populate `mixBuffer_` via
    /// `mixBufferChannel(int)` BEFORE invoking `process`.
    ///
    /// `const noexcept` because `OutputMixer::renderBuffer` is const and
    /// holds the Bus by const reference; the mix scratch is implementation
    /// detail the audio thread owns end-to-end via `mutable mixBuffer_`.
    ///
    /// Signature is JUCE-free for the same reason `ChannelStrip::process`
    /// is — the engine layer's public API stays free of `juce_audio_basics`
    /// (see engine/CMakeLists.txt header comment). JUCE-side callers wrap
    /// their `AudioBuffer<float>` via `getArrayOfWritePointers()` before
    /// calling.
    void process (float* const* output, int numChannels, int numSamples) const noexcept;

    /// Audio-thread write accessor — returns a pointer to the start of
    /// channel `c` inside `mixBuffer_`. Callers WRITE accumulated sends
    /// here before invoking `process`. Returns `nullptr` if `c` is out of
    /// range. `const` because the audio-thread caller holds a `const
    /// Bus&` (see class doc on `mutable mixBuffer_`).
    ///
    /// Layout: `mixBuffer_[c * kMaxBusMixSamples + s]`. The single-channel
    /// stride is `kMaxBusMixSamples`; callers must not write past the
    /// per-buffer `numSamples` they intend to mix.
    float* mixBufferChannel (int c) const noexcept;

private:
    BusId            id_;
    BusConfig        config_;
    EffectChain      effectChain_;

    /// Pre-allocated mix scratch. Sized to
    /// `kMaxBusMixSamples * kMaxBusChannelsHard` in the constructor; the
    /// audio thread only ever indexes this — never resizes. `mutable`
    /// because audio-thread callers write through `mixBufferChannel(int)
    /// const` (see class doc).
    mutable std::vector<float> mixBuffer_;
};

} // namespace sirius
