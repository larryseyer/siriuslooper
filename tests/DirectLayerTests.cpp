// Tests for ida::DirectLayer — M4 Session 1 registry surface.
// Session 1 ships the manual-route registry (addRawRoute /
// addProcessedRoute / removeRoute) and the OutputChannelId strong type;
// the audio-thread routeBuffers entry point lands in Session 2 and
// AudioCallback wiring lands in Session 3.
//
// Per V7 alignment plan M4 §"Sessions 1-3" (line 355).
#include "sirius/Channel.h"
#include "sirius/DirectLayer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <span>
#include <type_traits>
#include <vector>

using ida::ChannelId;
using ida::DirectLayer;
using ida::InputId;
using ida::OutputBufferView;
using ida::OutputChannelId;
using ida::ProcessedChannelBufferView;
using ida::RawInputBufferView;

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

// =====================================================================
// M4 Session 2 — routeBuffers audio-thread entry point
// =====================================================================

TEST_CASE ("DirectLayer::routeBuffers with no routes leaves outputs untouched",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;

    std::vector<float> outBuf (8, 0.25f);
    OutputBufferView   outView { OutputChannelId (0), outBuf.data(), static_cast<int> (outBuf.size()) };

    layer.routeBuffers ({}, {}, std::span<const OutputBufferView> (&outView, 1));

    for (float s : outBuf)
        CHECK (s == 0.25f);
}

TEST_CASE ("DirectLayer::routeBuffers mixes a single RawRoute into the matching output",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addRawRoute (InputId (7), OutputChannelId (3));

    const std::vector<float> inBuf  { 0.1f, 0.2f, 0.3f, 0.4f };
    std::vector<float>       outBuf { 0.0f, 0.0f, 0.0f, 0.0f };

    RawInputBufferView in  { InputId (7),          inBuf.data(),  static_cast<int> (inBuf.size())  };
    OutputBufferView   out { OutputChannelId (3),  outBuf.data(), static_cast<int> (outBuf.size()) };

    layer.routeBuffers (std::span<const RawInputBufferView> (&in, 1),
                        {},
                        std::span<const OutputBufferView>   (&out, 1));

    for (std::size_t i = 0; i < outBuf.size(); ++i)
        CHECK (outBuf[i] == inBuf[i]);
}

TEST_CASE ("DirectLayer::routeBuffers mixes a single ProcessedRoute into the matching output",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addProcessedRoute (ChannelId (2), OutputChannelId (1));

    const std::vector<float> chanBuf { 0.5f, -0.25f, 0.125f };
    std::vector<float>       outBuf  { 0.0f,  0.0f,   0.0f  };

    ProcessedChannelBufferView pc  { ChannelId (2),         chanBuf.data(), static_cast<int> (chanBuf.size()) };
    OutputBufferView           out { OutputChannelId (1),   outBuf.data(),  static_cast<int> (outBuf.size())  };

    layer.routeBuffers ({},
                        std::span<const ProcessedChannelBufferView> (&pc, 1),
                        std::span<const OutputBufferView>           (&out, 1));

    for (std::size_t i = 0; i < outBuf.size(); ++i)
        CHECK (outBuf[i] == chanBuf[i]);
}

TEST_CASE ("DirectLayer::routeBuffers additively sums two routes feeding the same output",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addRawRoute       (InputId (0),   OutputChannelId (0));
    layer.addProcessedRoute (ChannelId (0), OutputChannelId (0));

    const std::vector<float> in1 { 0.1f, 0.2f, 0.3f, 0.4f };
    const std::vector<float> in2 { 1.0f, 1.0f, 1.0f, 1.0f };
    std::vector<float>       out { 0.0f, 0.0f, 0.0f, 0.0f };

    RawInputBufferView         rv  { InputId (0),          in1.data(), static_cast<int> (in1.size()) };
    ProcessedChannelBufferView pv  { ChannelId (0),        in2.data(), static_cast<int> (in2.size()) };
    OutputBufferView           ov  { OutputChannelId (0),  out.data(), static_cast<int> (out.size()) };

    layer.routeBuffers (std::span<const RawInputBufferView>         (&rv, 1),
                        std::span<const ProcessedChannelBufferView> (&pv, 1),
                        std::span<const OutputBufferView>           (&ov, 1));

    for (std::size_t i = 0; i < out.size(); ++i)
        CHECK (out[i] == in1[i] + in2[i]);
}

TEST_CASE ("DirectLayer::routeBuffers silently skips a route whose source buffer is absent",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addRawRoute (InputId (9), OutputChannelId (0));

    std::vector<float> outBuf (4, 0.5f);
    OutputBufferView   out { OutputChannelId (0), outBuf.data(), static_cast<int> (outBuf.size()) };

    // No matching RawInputBufferView with InputId(9) in the spans.
    layer.routeBuffers ({}, {}, std::span<const OutputBufferView> (&out, 1));

    for (float s : outBuf)
        CHECK (s == 0.5f);
}

TEST_CASE ("DirectLayer::routeBuffers silently skips a route whose destination buffer is absent",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addRawRoute (InputId (0), OutputChannelId (99));  // dst not in `outputs`

    const std::vector<float> inBuf  { 1.0f, 2.0f, 3.0f, 4.0f };
    std::vector<float>       outBuf { 0.0f, 0.0f, 0.0f, 0.0f };

    RawInputBufferView in  { InputId (0),         inBuf.data(),  static_cast<int> (inBuf.size())  };
    OutputBufferView   out { OutputChannelId (0), outBuf.data(), static_cast<int> (outBuf.size()) };  // id mismatch on purpose

    layer.routeBuffers (std::span<const RawInputBufferView> (&in, 1),
                        {},
                        std::span<const OutputBufferView>   (&out, 1));

    for (float s : outBuf)
        CHECK (s == 0.0f);
}

TEST_CASE ("DirectLayer::routeBuffers honours min(src,dst) on size mismatch and never overruns",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addRawRoute (InputId (0), OutputChannelId (0));

    // src is twice as long as dst — the upper half of src must NOT be read,
    // and dst must remain exactly its declared size.
    std::vector<float> inBuf  (256, 1.0f);
    std::vector<float> outBuf (128, 0.0f);

    RawInputBufferView in  { InputId (0),         inBuf.data(),  static_cast<int> (inBuf.size())  };
    OutputBufferView   out { OutputChannelId (0), outBuf.data(), static_cast<int> (outBuf.size()) };

    layer.routeBuffers (std::span<const RawInputBufferView> (&in, 1),
                        {},
                        std::span<const OutputBufferView>   (&out, 1));

    REQUIRE (outBuf.size() == 128u);
    for (float s : outBuf)
        CHECK (s == 1.0f);

    // Reverse the mismatch: src shorter than dst — only the first src.size()
    // samples mix, the trailing dst tail stays at its prior value.
    std::vector<float> inShort  (64,  0.5f);
    std::vector<float> outLong (128, 0.25f);

    RawInputBufferView in2 { InputId (0),         inShort.data(), static_cast<int> (inShort.size()) };
    OutputBufferView   out2{ OutputChannelId (0), outLong.data(), static_cast<int> (outLong.size()) };

    layer.routeBuffers (std::span<const RawInputBufferView> (&in2, 1),
                        {},
                        std::span<const OutputBufferView>   (&out2, 1));

    for (std::size_t i = 0; i < 64; ++i)
        CHECK (outLong[i] == 0.25f + 0.5f);
    for (std::size_t i = 64; i < 128; ++i)
        CHECK (outLong[i] == 0.25f);
}

TEST_CASE ("DirectLayer::routeBuffers preserves pre-existing output content (additive, never assign)",
           "[direct-layer][route-buffers]")
{
    DirectLayer layer;
    layer.addRawRoute (InputId (0), OutputChannelId (0));

    const std::vector<float> preLoaded { 0.10f, 0.20f, 0.30f, 0.40f };
    const std::vector<float> inBuf     { 0.01f, 0.02f, 0.03f, 0.04f };
    std::vector<float>       outBuf    = preLoaded;

    RawInputBufferView in  { InputId (0),         inBuf.data(),  static_cast<int> (inBuf.size())  };
    OutputBufferView   out { OutputChannelId (0), outBuf.data(), static_cast<int> (outBuf.size()) };

    layer.routeBuffers (std::span<const RawInputBufferView> (&in, 1),
                        {},
                        std::span<const OutputBufferView>   (&out, 1));

    for (std::size_t i = 0; i < outBuf.size(); ++i)
        CHECK (outBuf[i] == preLoaded[i] + inBuf[i]);
}

TEST_CASE ("DirectLayer::routeBuffers stays well under the RT budget for a realistic load",
           "[direct-layer][route-buffers][.rt-smoke]")
{
    // Smoke test, not a benchmark. RT_SAFETY_CONTRACT §6 commits DirectLayer
    // to "well under 1 ms" on the audio thread. 4 raw + 4 processed routes
    // at 256 samples is a comfortable upper bound for a M4 deployment; we
    // run 100 warmup iterations (cold-cache discarded), then 100 timed
    // iterations and assert the **median** is <100µs. Median (not max)
    // avoids false flakes from OS scheduling jitter on a busy CI box, while
    // still catching any actual algorithmic regression — modern hardware
    // typically clocks <10µs for this workload, so the 100µs threshold is
    // very generous. Tagged hidden ([.rt-smoke]) so CI runs it on demand
    // only — register it under [route-buffers] for explicit ctest selection
    // during local tuning.
    DirectLayer layer;

    constexpr int kBlock         = 256;
    constexpr int kRawCount      = 4;
    constexpr int kProcessedCount = 4;

    std::vector<std::vector<float>> rawBuffers       (kRawCount,       std::vector<float> (kBlock, 0.1f));
    std::vector<std::vector<float>> processedBuffers (kProcessedCount, std::vector<float> (kBlock, 0.2f));
    std::vector<std::vector<float>> outBuffers       (kRawCount + kProcessedCount,
                                                      std::vector<float> (kBlock, 0.0f));

    std::vector<RawInputBufferView>         rawViews;
    std::vector<ProcessedChannelBufferView> procViews;
    std::vector<OutputBufferView>           outViews;
    rawViews.reserve  (kRawCount);
    procViews.reserve (kProcessedCount);
    outViews.reserve  (kRawCount + kProcessedCount);

    for (int i = 0; i < kRawCount; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        rawViews.push_back ({ InputId (i),         rawBuffers[idx].data(), kBlock });
        outViews.push_back ({ OutputChannelId (i), outBuffers[idx].data(), kBlock });
        layer.addRawRoute (InputId (i), OutputChannelId (i));
    }
    for (int i = 0; i < kProcessedCount; ++i)
    {
        const auto procIdx = static_cast<std::size_t> (i);
        const auto outIdx  = static_cast<std::size_t> (kRawCount + i);
        procViews.push_back ({ ChannelId (i),                    processedBuffers[procIdx].data(), kBlock });
        outViews.push_back  ({ OutputChannelId (kRawCount + i),  outBuffers[outIdx].data(),        kBlock });
        layer.addProcessedRoute (ChannelId (i), OutputChannelId (kRawCount + i));
    }

    const std::span<const RawInputBufferView>         rawSpan  (rawViews);
    const std::span<const ProcessedChannelBufferView> procSpan (procViews);
    const std::span<const OutputBufferView>           outSpan  (outViews);

    constexpr int kWarmup = 100;
    constexpr int kTimed  = 100;

    for (int i = 0; i < kWarmup; ++i)
        layer.routeBuffers (rawSpan, procSpan, outSpan);

    std::vector<long long> samplesUs;
    samplesUs.reserve (kTimed);
    for (int i = 0; i < kTimed; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        layer.routeBuffers (rawSpan, procSpan, outSpan);
        const auto t1 = std::chrono::steady_clock::now();
        samplesUs.push_back (std::chrono::duration_cast<std::chrono::microseconds> (t1 - t0).count());
    }

    std::nth_element (samplesUs.begin(),
                      samplesUs.begin() + samplesUs.size() / 2,
                      samplesUs.end());
    const auto medianUs = samplesUs[samplesUs.size() / 2];
    CHECK (medianUs < 100);
}
