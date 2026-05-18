// Tests for sirius::DirectLayer — M4 Session 1 registry surface.
// Session 1 ships the manual-route registry (addRawRoute /
// addProcessedRoute / removeRoute) and the OutputChannelId strong type;
// the audio-thread routeBuffers entry point lands in Session 2 and
// AudioCallback wiring lands in Session 3.
//
// Per V7 alignment plan M4 §"Sessions 1-3" (line 355).
#include "sirius/Channel.h"
#include "sirius/DirectLayer.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using sirius::ChannelId;
using sirius::DirectLayer;
using sirius::InputId;
using sirius::OutputChannelId;

static_assert (! std::is_convertible_v<int, OutputChannelId>,
               "OutputChannelId must NOT be implicitly constructible from int — strong typing");
static_assert (std::is_default_constructible_v<DirectLayer>,
               "DirectLayer must remain default-constructible");
static_assert (std::is_destructible_v<DirectLayer>);

TEST_CASE ("OutputChannelId is a strong-typed wrapper around int64",
           "[channel][output-channel-id]")
{
    // Matches house TapeId / InputId / ChannelId pattern — constexpr ctor,
    // non-constexpr value() / == / != per the M2 Session 3 deviation note.
    const OutputChannelId a (11);
    const OutputChannelId b (11);
    const OutputChannelId c (12);

    CHECK (a.value() == 11);
    CHECK (a == b);
    CHECK (a != c);
}

TEST_CASE ("DirectLayer is default-constructible and destructible without crashing",
           "[direct-layer]")
{
    DirectLayer layer;
    CHECK (layer.rawRouteCount() == 0u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("DirectLayer::addRawRoute returns distinct RouteIds for distinct calls",
           "[direct-layer]")
{
    DirectLayer layer;

    const auto r0 = layer.addRawRoute (InputId (0), OutputChannelId (0));
    const auto r1 = layer.addRawRoute (InputId (1), OutputChannelId (0));
    const auto r2 = layer.addRawRoute (InputId (0), OutputChannelId (1));

    CHECK (r0 != r1);
    CHECK (r1 != r2);
    CHECK (r0 != r2);
    CHECK (layer.rawRouteCount() == 3u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("DirectLayer::addProcessedRoute returns distinct RouteIds from addRawRoute",
           "[direct-layer]")
{
    // The raw-vs-processed split is the key invariant: a RouteId carries
    // an internal kind tag so removeRoute hits the right table. Test asserts
    // that two calls — one raw, one processed — using the same numeric
    // source/destination produce non-equal handles.
    DirectLayer layer;

    const auto rawId       = layer.addRawRoute       (InputId (5),   OutputChannelId (0));
    const auto processedId = layer.addProcessedRoute (ChannelId (5), OutputChannelId (0));

    CHECK (rawId != processedId);
    CHECK (layer.rawRouteCount() == 1u);
    CHECK (layer.processedRouteCount() == 1u);
}

TEST_CASE ("DirectLayer::removeRoute drops the matching route from its kind's count",
           "[direct-layer]")
{
    DirectLayer layer;

    const auto rawA = layer.addRawRoute       (InputId (0),   OutputChannelId (0));
    const auto rawB = layer.addRawRoute       (InputId (1),   OutputChannelId (0));
    const auto procA = layer.addProcessedRoute (ChannelId (0), OutputChannelId (0));

    REQUIRE (layer.rawRouteCount()       == 2u);
    REQUIRE (layer.processedRouteCount() == 1u);

    layer.removeRoute (rawA);
    CHECK (layer.rawRouteCount()       == 1u);
    CHECK (layer.processedRouteCount() == 1u);

    layer.removeRoute (procA);
    CHECK (layer.rawRouteCount()       == 1u);
    CHECK (layer.processedRouteCount() == 0u);

    layer.removeRoute (rawB);
    CHECK (layer.rawRouteCount()       == 0u);
    CHECK (layer.processedRouteCount() == 0u);
}

TEST_CASE ("DirectLayer::removeRoute on an already-removed route is a silent no-op in release",
           "[direct-layer]")
{
    // Documented behavior: double-remove asserts in debug (programmer
    // error) and silently no-ops in release. With generation-counter
    // handles, the second removeRoute scans the (now smaller) vector,
    // finds no entry whose generation matches, and falls through to the
    // assert (debug) / silent return (release). NDEBUG builds exercise
    // the silent path; the assert in debug builds catches the bug at the
    // source. This test only asserts the observable-state invariant.
#ifdef NDEBUG
    DirectLayer layer;
    const auto id = layer.addRawRoute (InputId (0), OutputChannelId (0));
    REQUIRE (layer.rawRouteCount() == 1u);

    layer.removeRoute (id);
    CHECK (layer.rawRouteCount() == 0u);

    layer.removeRoute (id);  // double-remove
    CHECK (layer.rawRouteCount() == 0u);
#else
    SUCCEED ("debug build — double-remove asserts; covered by NDEBUG path");
#endif
}

TEST_CASE ("DirectLayer::addRawRoute after removeRoute mints a fresh, distinct RouteId",
           "[direct-layer]")
{
    // Storage is dense (swap-and-pop on removal) so the freed slot can be
    // reused for the next push_back. The generation counter guarantees the
    // new RouteId still compares unequal to the removed handle — the
    // caller's stale handle cannot accidentally name the new route. This
    // is the property that makes double-remove detectable in debug.
    DirectLayer layer;

    const auto first = layer.addRawRoute (InputId (0), OutputChannelId (0));
    REQUIRE (layer.rawRouteCount() == 1u);

    layer.removeRoute (first);
    REQUIRE (layer.rawRouteCount() == 0u);

    const auto second = layer.addRawRoute (InputId (0), OutputChannelId (0));
    CHECK (layer.rawRouteCount() == 1u);
    CHECK (second != first);
}

TEST_CASE ("DirectLayer::addProcessedRoute after removeRoute mints a fresh, distinct RouteId",
           "[direct-layer]")
{
    // Same generation-counter property as the raw-route variant above,
    // exercised on the processed-route table to confirm the two
    // monotonic counters (raw vs processed) are independent.
    DirectLayer layer;

    const auto first = layer.addProcessedRoute (ChannelId (0), OutputChannelId (0));
    REQUIRE (layer.processedRouteCount() == 1u);

    layer.removeRoute (first);
    REQUIRE (layer.processedRouteCount() == 0u);

    const auto second = layer.addProcessedRoute (ChannelId (0), OutputChannelId (0));
    CHECK (layer.processedRouteCount() == 1u);
    CHECK (second != first);
}
