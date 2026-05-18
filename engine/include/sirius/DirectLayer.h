#pragma once

#include "sirius/Channel.h"

#include <cstdint>
#include <vector>

namespace sirius
{

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
    RouteId addRawRoute       (InputId   source, OutputChannelId destination);
    RouteId addProcessedRoute (ChannelId source, OutputChannelId destination);

    // Removes a previously-added route. Message thread. Looks up the
    // matching entry by generation and does swap-and-pop. A double-remove
    // (or any handle that doesn't match a live entry) is a programmer
    // error and asserts in debug; in release the call is a silent no-op
    // so a misbehaving caller cannot corrupt the route table.
    void removeRoute (RouteId);

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
    };

    struct ProcessedRoute
    {
        ChannelId       source;
        OutputChannelId destination;
        std::int64_t    generation;
    };

    std::vector<RawRoute>       rawRoutes_;
    std::vector<ProcessedRoute> processedRoutes_;
    std::int64_t                nextRawGeneration_       { 1 };
    std::int64_t                nextProcessedGeneration_ { 1 };
};

} // namespace sirius
