// 2026-05-24 monitor slice — the operator's failing-case test for the
// mute leak: muting a channel that has direct-layer monitoring enabled
// must silence the monitor signal too, regardless of which tap (Raw =
// pre-strip, Processed = post-strip) the mode selects. This pins the
// kill-switch reading of mute (whitepaper §7 + the 2026-05-24 fix).
//
// The fix path runs entirely on the DirectLayer level — InputMixer passes
// the strip's mute atomic into the DirectLayer route at registration time,
// and DirectLayer::routeBuffers checks that atomic before accumulating.
// These tests verify the DirectLayer side directly because the audio-
// thread monitor-output buffer is what the OutputMixer master meter
// observes (and what was leaking signal in the reported bug).
#include "ida/Channel.h"
#include "ida/DirectLayer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <span>

using ida::DirectLayer;
using ida::InputId;
using ida::OutputBufferView;
using ida::OutputChannelId;
using ida::ProcessedChannelBufferView;
using ida::RawInputBufferView;

namespace
{
    constexpr int kBlockSamples = 16;

    bool bufferIsSilent (const std::array<float, kBlockSamples>& buf)
    {
        for (float s : buf) if (s != 0.0f) return false;
        return true;
    }
}

TEST_CASE ("DirectLayer raw route: muteFlag pointer nullptr keeps legacy passthrough behavior",
           "[direct-layer][monitor][mute-leak]")
{
    DirectLayer layer;
    layer.addRawRoute (InputId (0), OutputChannelId (0)); // legacy form — no mute pointer

    std::array<float, kBlockSamples> src;
    std::array<float, kBlockSamples> dst;
    src.fill (0.5f);
    dst.fill (0.0f);

    const std::array<RawInputBufferView, 1> rawInputs {
        RawInputBufferView { InputId (0), src.data(), kBlockSamples } };
    const std::array<OutputBufferView, 1> outputs {
        OutputBufferView { OutputChannelId (0), dst.data(), kBlockSamples } };

    layer.routeBuffers (rawInputs, {}, outputs);
    CHECK (! bufferIsSilent (dst));
}

TEST_CASE ("DirectLayer raw route: muted source -> destination stays silent",
           "[direct-layer][monitor][mute-leak]")
{
    // The operator's failing case at the DirectLayer level: a Raw route
    // taps the physical input pre-strip, so without the mute-flag pointer
    // the strip's mute would have no effect on this route. The fix is the
    // muteFlag stamped on the route; flipping it true makes routeBuffers
    // skip the accumulation.
    std::atomic<bool> mute { false };

    DirectLayer layer;
    layer.addRawRoute (InputId (0), OutputChannelId (0), &mute);

    std::array<float, kBlockSamples> src;
    std::array<float, kBlockSamples> dst;
    src.fill (0.5f);
    dst.fill (0.0f);

    const std::array<RawInputBufferView, 1> rawInputs {
        RawInputBufferView { InputId (0), src.data(), kBlockSamples } };
    const std::array<OutputBufferView, 1> outputs {
        OutputBufferView { OutputChannelId (0), dst.data(), kBlockSamples } };

    // Unmuted: signal flows through.
    layer.routeBuffers (rawInputs, {}, outputs);
    REQUIRE (! bufferIsSilent (dst));

    // Mute and re-render with a zeroed destination — the route must skip.
    dst.fill (0.0f);
    mute.store (true);
    layer.routeBuffers (rawInputs, {}, outputs);
    CHECK (bufferIsSilent (dst));

    // Unmute again — signal returns. This catches a bug where mute would
    // permanently latch the route off.
    dst.fill (0.0f);
    mute.store (false);
    layer.routeBuffers (rawInputs, {}, outputs);
    CHECK (! bufferIsSilent (dst));
}

TEST_CASE ("DirectLayer processed route: muted source -> destination stays silent",
           "[direct-layer][monitor][mute-leak]")
{
    // Symmetric coverage for Processed: even though the strip's own
    // process() already zeroes the post-strip buffer when muted, the
    // mute-flag gate on the processed route is still load-bearing because
    // some Processed tap points (future slices) may run upstream of the
    // strip mute check. Belt-and-suspenders aligned with the operator's
    // kill-switch expectation.
    std::atomic<bool> mute { false };

    DirectLayer layer;
    layer.addProcessedRoute (ida::ChannelId (1), OutputChannelId (0), &mute);

    std::array<float, kBlockSamples> src;
    std::array<float, kBlockSamples> dst;
    src.fill (0.5f);
    dst.fill (0.0f);

    const std::array<ProcessedChannelBufferView, 1> processed {
        ProcessedChannelBufferView { ida::ChannelId (1), src.data(), kBlockSamples } };
    const std::array<OutputBufferView, 1> outputs {
        OutputBufferView { OutputChannelId (0), dst.data(), kBlockSamples } };

    layer.routeBuffers ({}, processed, outputs);
    REQUIRE (! bufferIsSilent (dst));

    dst.fill (0.0f);
    mute.store (true);
    layer.routeBuffers ({}, processed, outputs);
    CHECK (bufferIsSilent (dst));
}

TEST_CASE ("DirectLayer: multiple routes — mute is per-route, not global",
           "[direct-layer][monitor][mute-leak]")
{
    // Two channels on the same DirectLayer; muting one must not affect
    // the other. This pins the per-route mute scoping (one atomic per
    // route, not a shared global flag).
    std::atomic<bool> muteA { false };
    std::atomic<bool> muteB { false };

    DirectLayer layer;
    layer.addRawRoute (InputId (0), OutputChannelId (0), &muteA);
    layer.addRawRoute (InputId (1), OutputChannelId (1), &muteB);

    std::array<float, kBlockSamples> srcA, srcB;
    std::array<float, kBlockSamples> dstA, dstB;
    srcA.fill (0.5f);
    srcB.fill (0.5f);
    dstA.fill (0.0f);
    dstB.fill (0.0f);

    const std::array<RawInputBufferView, 2> rawInputs {
        RawInputBufferView { InputId (0), srcA.data(), kBlockSamples },
        RawInputBufferView { InputId (1), srcB.data(), kBlockSamples } };
    const std::array<OutputBufferView, 2> outputs {
        OutputBufferView { OutputChannelId (0), dstA.data(), kBlockSamples },
        OutputBufferView { OutputChannelId (1), dstB.data(), kBlockSamples } };

    muteA.store (true);
    layer.routeBuffers (rawInputs, {}, outputs);
    CHECK (bufferIsSilent (dstA));        // A's route is muted
    CHECK (! bufferIsSilent (dstB));      // B's route is still hot
}
