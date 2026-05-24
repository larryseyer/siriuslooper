#pragma once

#include "ida/ITapeSink.h"
#include "ida/TapeColorMode.h"
#include "ida/TapeId.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace lsfx::tapecolor { struct TapeColorConfig; }

namespace ida
{

class TapeColorAdapter;

/// TAPECOLOR Slice 2 — the per-tape TAPECOLOR routing decorator.
///
/// Sits between the `InputMixer`'s per-tape sum (the `ITapeSink` boundary)
/// and the downstream sink (`FlacTapeSink` in the standalone app). For tapes
/// whose mode is `BeforeWrite` it routes each block through that tape's
/// `TapeColorAdapter` before forwarding to the inner sink — the color is
/// baked into whatever the inner sink persists.
///
/// `None` and `AfterRead` modes are bit-identical passthroughs on the write
/// path. `AfterRead` is the playback-only path, applied by a separate sink
/// downstream of the tape-read side (lands in a follow-on slice once a tape-
/// read path exists). The two modes can coexist on the same tape — legal,
/// silent, no UI warning per the operator design lock 2026-05-24.
///
/// Lifetime: per-tape adapters live as long as the tape is registered
/// (`addTape` / `removeTape`, message-thread only). `setMode` and the config
/// helpers are message-thread only. `deliverTapeBlock` is audio-thread;
/// it's `noexcept`, allocation/lock/I/O-free per the RT-safety contract.
///
/// Capacity: pre-reserves `kMaxTapes` slots so the underlying vector never
/// reallocates after construction (mirrors `InputMixer::tapeTerminals_`).
/// Mutations happen on the message thread; the audio thread reads the live
/// snapshot — racing reads see either the pre- or post-mutation state, never
/// torn pointers.
class TapeColoringSink final : public ITapeSink
{
public:
    /// Mirrors `InputMixer::kMaxTapes`. Pre-reserves the per-tape slot
    /// table so audio-thread reads never observe a reallocation.
    static constexpr int kMaxTapes = 64;

    /// `innerSink` is the downstream sink — typically the standalone app's
    /// `FlacTapeSink`. Non-owning; the inner sink must outlive this object.
    /// `sampleRate` / `maxBlockSize` are used to `prepare()` the per-tape
    /// adapters as they are added.
    TapeColoringSink (ITapeSink* innerSink, double sampleRate, int maxBlockSize);
    ~TapeColoringSink() override;

    TapeColoringSink (const TapeColoringSink&) = delete;
    TapeColoringSink& operator= (const TapeColoringSink&) = delete;

    /// Audio-thread entry. Looks up `tape`; if BeforeWrite with a prepared
    /// adapter, colors into the internal scratch buffer and forwards;
    /// otherwise forwards `left`/`right` bit-identical.
    void deliverTapeBlock (TapeId tape, const float* left, const float* right,
                           int numSamples) noexcept override;

    // ── Message-thread tape registry ──────────────────────────────────
    /// Adds a tape with mode `None`. Idempotent: a no-op if the tape is
    /// already registered. Returns false on capacity miss.
    bool addTape (TapeId tape);

    /// Removes a tape's registration. No-op if the tape isn't registered.
    /// After this returns, `deliverTapeBlock` for `tape` is passthrough.
    bool removeTape (TapeId tape);

    /// Sets the per-tape mode. No-op (returns false) for an unknown tape.
    bool setMode (TapeId tape, TapeColorMode mode);

    /// Returns the current mode for `tape`, or `None` if unknown.
    TapeColorMode modeFor (TapeId tape) const noexcept;

    /// Message-thread only — call with the audio callback detached. Re-
    /// prepares every registered tape's adapter against the new device
    /// configuration. Future-added tapes prepare against the latest values
    /// passed here.
    void setSampleRate (double sampleRate, int maxBlockSize);

    // ── Message-thread parameter publication (per tape) ───────────────
    /// Mirrors the wrapped adapter's config-swap API, but keyed by tape.
    /// Throws `std::out_of_range` if the tape isn't registered (a misuse
    /// — register first via `addTape`, then commit config).
    lsfx::tapecolor::TapeColorConfig&       scratchConfig (TapeId tape);
    void                                    commitConfig (TapeId tape);
    const lsfx::tapecolor::TapeColorConfig& liveConfig    (TapeId tape) const;

private:
    struct Entry
    {
        std::int64_t                      tapeId;
        std::atomic<TapeColorMode>        mode { TapeColorMode::None };
        std::unique_ptr<TapeColorAdapter> adapter;
    };

    const Entry* findEntry (TapeId tape) const noexcept;
    Entry*       findEntry (TapeId tape) noexcept;

    ITapeSink* innerSink_;
    double     sampleRate_;
    int        maxBlockSize_;

    // Pre-reserved at ctor (kMaxTapes); never reallocates.
    std::vector<std::unique_ptr<Entry>> entries_;

    // Audio-thread scratch for the BeforeWrite path. Sized to maxBlockSize
    // at ctor time. Allocation-free in `deliverTapeBlock`.
    std::vector<float> scratchLeft_;
    std::vector<float> scratchRight_;
};

} // namespace ida
