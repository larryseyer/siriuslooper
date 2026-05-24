#pragma once

#include "ida/Channel.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace ida
{

/// Non-owning view of a raw input buffer for a single AudioCallback block.
/// Constructed and consumed within one `DirectLayer::routeBuffers` call;
/// the pointer must stay valid for the duration of that call only.
//
// RawInputBufferView wraps a raw float input buffer as it arrives from the
// host audio device callback (e.g. JUCE's audioDeviceIOCallbackWithContext
// inputChannelData[ch]) — NOT the std::byte-typed payload that M3's
// InputMixer::processBuffer serializes for tape. The DirectLayer operates
// on the live audio-thread float buffer pre-serialization.
struct RawInputBufferView
{
    InputId      id;
    const float* samples;
    int          sampleCount;
};

/// Non-owning view of a processed channel buffer for a single AudioCallback
/// block. Same lifetime contract as `RawInputBufferView`.
struct ProcessedChannelBufferView
{
    ChannelId    id;
    const float* samples;
    int          sampleCount;
};

/// Non-owning writable view of an output buffer for a single AudioCallback
/// block. `routeBuffers` performs additive mix into `samples`; the caller
/// (Session 3 AudioCallback) is responsible for the buffer's initial
/// contents (typically zero-filled or pre-loaded by OutputMixer).
struct OutputBufferView
{
    OutputChannelId id;
    float*          samples;
    int             sampleCount;
};

/// V3 §2.3 / V7 alignment plan M4: the Direct Layer is the parallel
/// signal path from input mixer to output mixer that bypasses the tape
/// entirely. Two route kinds: a raw route forwards an `InputId` straight
/// to an `OutputChannelId` (pre-processing), a processed route forwards
/// a `ChannelId` after its `ProcessingChain` has run. Both kinds bypass
/// tape regardless of the channel's `TapeMode`.
///
/// M4 ships **manual routing only** (V7 plan M4 Risks line 369: "Auto
/// inference is explicitly deferred to M14"). The audio-thread
/// `routeBuffers` entry point lands in Session 2; this header is the
/// Session 1 registry surface only.
///
/// Registry calls are message-thread (caller-supplied IDs come from the
/// app layer); they can throw on allocation failure. Audio-thread
/// methods added in Session 2 will be `noexcept`.
///
/// Threading contract:
///   - addRawRoute / addProcessedRoute / removeRoute: message thread only.
///     May allocate. May throw on allocation failure (std::bad_alloc).
///   - routeBuffers (Session 2): audio thread only. noexcept.
///   - Route registration must happen BEFORE the audio thread begins
///     reading the route table, or while the audio thread is provably
///     idle. This matches the InputMixer / OutputMixer / TapeStore
///     message-thread-configures pattern locked in M3. No atomic-snapshot
///     or lock-free publish here — adding that is a project-wide decision,
///     not a per-component one.
///
/// Storage shape:
///   Routes are kept in **dense** vectors with no tombstones. `removeRoute`
///   does swap-and-pop, so `rawRoutes_.size()` always equals the live
///   route count and the Session 2 audio thread can iterate without an
///   `if (active)` branch — bounded by current active count, not history
///   (RT_SAFETY_CONTRACT §1). Handles are stable across removals because
///   `RouteId` carries a monotonic per-kind generation counter, NOT a
///   vector index — a stale handle from a removed route compares unequal
///   to any live entry and asserts on double-remove in debug.
class DirectLayer
{
public:
    DirectLayer();
    ~DirectLayer();

    /// Opaque handle returned by addRawRoute / addProcessedRoute and
    /// accepted by removeRoute. Same shape as the other strong-typed IDs
    /// in this header set (ChannelId / InputId / OutputChannelId), with
    /// one extra bit of tag state so removeRoute knows which table to
    /// scan without a separate lookup. The tag is opaque to callers; the
    /// generation counter is intentionally not exposed — callers treat
    /// the handle as an opaque token and only ever compare it for
    /// equality or hand it back to removeRoute.
    class RouteId
    {
    public:
        enum class Kind : std::int8_t { Raw, Processed };

        Kind kind() const noexcept { return kind_; }

        bool operator== (const RouteId& other) const noexcept
        {
            return kind_ == other.kind_ && generation_ == other.generation_;
        }
        bool operator!= (const RouteId& other) const noexcept { return ! (*this == other); }

    private:
        constexpr RouteId (Kind kind, std::int64_t generation) noexcept
            : kind_ (kind), generation_ (generation) {}

        Kind         kind_;
        std::int64_t generation_;

        friend class DirectLayer;
    };

    // Manual route registration (message thread; may throw on allocation).
    // Returns a RouteId the caller stashes for later removeRoute.
    //
    // `muteFlag` (optional) is a non-owning pointer to a mute atomic the
    // audio-thread `routeBuffers` reads to decide whether to accumulate this
    // route's signal. nullptr (default) preserves the pre-2026-05-24 behavior
    // (route always contributes). A non-null pointer makes the operator's
    // mute on the source channel a kill-switch for the monitor signal too —
    // see ChannelStrip::mutedAtomic. The pointer MUST outlive every audio-
    // thread call until removeRoute returns; InputMixer manages this by
    // removing the route before destroying the strip that owns the atomic.
    RouteId addRawRoute       (InputId   source, OutputChannelId destination,
                               const std::atomic<bool>* muteFlag = nullptr);
    RouteId addProcessedRoute (ChannelId source, OutputChannelId destination,
                               const std::atomic<bool>* muteFlag = nullptr);

    // Removes a previously-added route. Message thread. Looks up the
    // matching entry by generation and does swap-and-pop. A double-remove
    // (or any handle that doesn't match a live entry) is a programmer
    // error and asserts in debug; in release the call is a silent no-op
    // so a misbehaving caller cannot corrupt the route table.
    void removeRoute (RouteId);

    /// Audio-thread entry point. For each registered route, locates the
    /// matching source buffer and destination buffer in the supplied spans
    /// and **mix-adds** the source samples into the destination. The
    /// destination is never overwritten — DirectLayer contributes alongside
    /// OutputMixer, which also writes into the same output buffers.
    ///
    /// Threading: audio thread only. `noexcept`. No allocation, no locks,
    /// no logging, no I/O, no map lookups. Lookups are linear scans over
    /// the supplied buffer spans (bounded by buffer count, typically <16).
    /// Cost is bounded by `route count × buffer count` and is allocation-
    /// free; this matches the RT_SAFETY_CONTRACT §6 commitment.
    ///
    /// Buffer-size mismatch between a route's source and destination is
    /// handled by mixing only `min(src.sampleCount, dst.sampleCount)`
    /// samples — never reads past the source, never writes past the
    /// destination. A route whose source or destination is absent from
    /// the supplied spans is skipped silently; this is expected in normal
    /// operation when a buffer is not present in the current callback.
    ///
    /// Gain control is intentionally NOT part of this signature — Session 2
    /// ships pure pass-through routing. Per-route gain (with an atomic gain
    /// field per route) is a future addition tracked in the V7 plan; the
    /// RT_SAFETY_CONTRACT §6 wording explicitly anticipates "reading a
    /// couple of gain atomics" on the hot path when that lands.
    ///
    /// Caller responsibility — span storage MUST NOT allocate on the audio
    /// thread. AudioCallback (Session 3) is expected to either:
    ///   (a) hold a fixed-capacity std::array<RawInputBufferView, kMax> as a
    ///       message-thread-configured member, or
    ///   (b) pre-allocate std::vector<RawInputBufferView> scratch in
    ///       audioDeviceAboutToStart and only mutate (never resize) it in
    ///       the callback.
    /// Constructing spans from heap-resized vectors inside the callback would
    /// violate RT_SAFETY_CONTRACT §6.
    void routeBuffers (std::span<const RawInputBufferView>         rawInputs,
                       std::span<const ProcessedChannelBufferView> processedChannels,
                       std::span<const OutputBufferView>           outputs) const noexcept;

    // Diagnostic accessors — Session 1 test surface only. Audio-thread
    // routeBuffers (Session 2) will iterate the active subset directly.
    // O(1) because storage is dense (no tombstones).
    std::size_t rawRouteCount() const noexcept;
    std::size_t processedRouteCount() const noexcept;

private:
    struct RawRoute
    {
        InputId         source;
        OutputChannelId destination;
        std::int64_t    generation;   // stamped at insertion; matches RouteId::generation_
        // nullptr = always contributes (legacy/test seam). Non-null = audio
        // thread reads it once per block; non-zero skips this route.
        const std::atomic<bool>* muteFlag { nullptr };
    };

    struct ProcessedRoute
    {
        ChannelId       source;
        OutputChannelId destination;
        std::int64_t    generation;
        const std::atomic<bool>* muteFlag { nullptr };
    };

    std::vector<RawRoute>       rawRoutes_;
    std::vector<ProcessedRoute> processedRoutes_;
    std::int64_t                nextRawGeneration_       { 1 };
    std::int64_t                nextProcessedGeneration_ { 1 };
};

} // namespace ida
