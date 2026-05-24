// Tests for ida::TapeColoringSink — the per-tape TAPECOLOR routing decorator
// (TAPECOLOR Slice 2). Sits between InputMixer's per-tape sum and the
// downstream sink (FlacTapeSink in MainComponent). For tapes whose mode is
// BeforeWrite it routes the block through the tape's TapeColorAdapter before
// forwarding to the inner sink; for None and AfterRead it forwards bit-
// identically (AfterRead colors on playback, downstream of this sink).
//
// We do NOT test DSP correctness (lsfx_tapecolor's phases own that); we test
// the routing: which tapes get colored when, and that lifetime (add/remove)
// works without crash.

#include "ida/TapeColoringSink.h"

#include "ida/ITapeSink.h"
#include "ida/TapeColorMode.h"
#include "ida/TapeId.h"

#include <lsfx_tapecolor/lsfx_tapecolor.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

using ida::TapeColoringSink;
using ida::TapeColorMode;
using ida::TapeId;

namespace
{

class RecordingTapeSink : public ida::ITapeSink
{
public:
    struct Block
    {
        std::vector<float> left;
        std::vector<float> right;
    };

    std::unordered_map<std::int64_t, std::vector<Block>> received;

    void deliverTapeBlock (TapeId tape, const float* l, const float* r, int n) noexcept override
    {
        Block b;
        b.left.assign  (l, l + n);
        b.right.assign (r, r + n);
        received[tape.value()].push_back (std::move (b));
    }
};

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 64;

std::vector<float> rampBlock (float start, float step, int n)
{
    std::vector<float> v (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        v[static_cast<std::size_t> (i)] = start + step * static_cast<float> (i);
    return v;
}

} // namespace

TEST_CASE ("TapeColoringSink with no registered tapes forwards bit-identical",
           "[tape-coloring-sink][tapecolor]")
{
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    auto L = rampBlock ( 0.10f,  0.001f, kBlockSize);
    auto R = rampBlock (-0.20f, -0.002f, kBlockSize);

    sink.deliverTapeBlock (TapeId (1), L.data(), R.data(), kBlockSize);

    REQUIRE (inner.received[1].size() == 1u);
    CHECK   (inner.received[1][0].left  == L);
    CHECK   (inner.received[1][0].right == R);
}

TEST_CASE ("TapeColoringSink with a registered tape (mode None) forwards bit-identical",
           "[tape-coloring-sink][tapecolor]")
{
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.addTape (TapeId (3));
    CHECK (sink.modeFor (TapeId (3)) == TapeColorMode::None);

    auto L = rampBlock (0.1f,  0.001f, kBlockSize);
    auto R = rampBlock (0.2f, -0.001f, kBlockSize);
    sink.deliverTapeBlock (TapeId (3), L.data(), R.data(), kBlockSize);

    REQUIRE (inner.received[3].size() == 1u);
    CHECK   (inner.received[3][0].left  == L);
    CHECK   (inner.received[3][0].right == R);
}

TEST_CASE ("TapeColoringSink BeforeWrite with a default-OFF processor is still passthrough",
           "[tape-coloring-sink][tapecolor]")
{
    // Pins the TAPECOLOR default-OFF rule (operator design lock 2026-05-24).
    // Even when a tape is in BeforeWrite mode, the processor's enabled flag
    // defaults to false — the signal must arrive at the inner sink
    // bit-identical until the operator explicitly enables the processor
    // through scratchConfig() + commitConfig().
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.addTape (TapeId (7));
    sink.setMode (TapeId (7), TapeColorMode::BeforeWrite);
    CHECK (sink.modeFor (TapeId (7)) == TapeColorMode::BeforeWrite);

    auto L = rampBlock (0.05f,  0.0005f, kBlockSize);
    auto R = rampBlock (0.06f, -0.0007f, kBlockSize);
    sink.deliverTapeBlock (TapeId (7), L.data(), R.data(), kBlockSize);

    REQUIRE (inner.received[7].size() == 1u);
    CHECK   (inner.received[7][0].left  == L);
    CHECK   (inner.received[7][0].right == R);
}

TEST_CASE ("TapeColoringSink BeforeWrite with an enabled processor alters the signal",
           "[tape-coloring-sink][tapecolor]")
{
    // When the processor is turned on (cfg.enabled = true) and the tape's
    // mode is BeforeWrite, the bytes the inner sink receives must differ
    // from the input — this is the actual "color is baked into the FLAC"
    // contract. We don't assert WHAT changed (that's lsfx_tapecolor's
    // territory), only that SOMETHING changed.
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.addTape (TapeId (4));
    sink.setMode (TapeId (4), TapeColorMode::BeforeWrite);

    auto& cfg = sink.scratchConfig (TapeId (4));
    cfg.enabled = true;
    sink.commitConfig (TapeId (4));

    auto L = rampBlock (0.10f,  0.002f, kBlockSize);
    auto R = rampBlock (0.15f, -0.001f, kBlockSize);
    sink.deliverTapeBlock (TapeId (4), L.data(), R.data(), kBlockSize);

    REQUIRE (inner.received[4].size() == 1u);
    const auto& got = inner.received[4][0];

    bool anyDifferent = false;
    for (int i = 0; i < kBlockSize; ++i)
        if (got.left [static_cast<std::size_t> (i)] != L[static_cast<std::size_t> (i)]
         || got.right[static_cast<std::size_t> (i)] != R[static_cast<std::size_t> (i)])
        {
            anyDifferent = true;
            break;
        }
    CHECK (anyDifferent);
}

TEST_CASE ("TapeColoringSink AfterRead does NOT color on the write path",
           "[tape-coloring-sink][tapecolor]")
{
    // AfterRead = clean on disk, color on playback. So even with an enabled
    // processor, the BeforeWrite-time hook (this sink) must forward
    // bit-identical and leave coloring to the (future-slice) playback hook.
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.addTape (TapeId (5));
    sink.setMode (TapeId (5), TapeColorMode::AfterRead);

    // Enable the processor — AfterRead mode must still bypass it on write.
    auto& cfg = sink.scratchConfig (TapeId (5));
    cfg.enabled = true;
    sink.commitConfig (TapeId (5));

    auto L = rampBlock (0.10f, 0.001f, kBlockSize);
    auto R = rampBlock (0.20f, 0.002f, kBlockSize);
    sink.deliverTapeBlock (TapeId (5), L.data(), R.data(), kBlockSize);

    REQUIRE (inner.received[5].size() == 1u);
    CHECK   (inner.received[5][0].left  == L);
    CHECK   (inner.received[5][0].right == R);
}

TEST_CASE ("TapeColoringSink removeTape: subsequent deliver is passthrough, no crash",
           "[tape-coloring-sink][tapecolor]")
{
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.addTape (TapeId (8));
    sink.setMode (TapeId (8), TapeColorMode::BeforeWrite);
    sink.removeTape (TapeId (8));

    auto L = rampBlock (0.1f, 0.001f, kBlockSize);
    auto R = rampBlock (0.2f, 0.002f, kBlockSize);
    sink.deliverTapeBlock (TapeId (8), L.data(), R.data(), kBlockSize);

    REQUIRE (inner.received[8].size() == 1u);
    CHECK   (inner.received[8][0].left  == L);
    CHECK   (inner.received[8][0].right == R);
}

TEST_CASE ("TapeColoringSink routes multiple tapes independently",
           "[tape-coloring-sink][tapecolor]")
{
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.addTape (TapeId (1));  // None
    sink.addTape (TapeId (2));  // BeforeWrite enabled — should change
    sink.addTape (TapeId (3));  // AfterRead enabled — must NOT change

    sink.setMode (TapeId (2), TapeColorMode::BeforeWrite);
    {   auto& c = sink.scratchConfig (TapeId (2)); c.enabled = true;
        sink.commitConfig (TapeId (2)); }

    sink.setMode (TapeId (3), TapeColorMode::AfterRead);
    {   auto& c = sink.scratchConfig (TapeId (3)); c.enabled = true;
        sink.commitConfig (TapeId (3)); }

    auto L = rampBlock (0.10f, 0.002f, kBlockSize);
    auto R = rampBlock (0.20f, 0.001f, kBlockSize);

    sink.deliverTapeBlock (TapeId (1), L.data(), R.data(), kBlockSize);
    sink.deliverTapeBlock (TapeId (2), L.data(), R.data(), kBlockSize);
    sink.deliverTapeBlock (TapeId (3), L.data(), R.data(), kBlockSize);

    // Tape 1 (None): bit-identical.
    CHECK (inner.received[1][0].left  == L);
    CHECK (inner.received[1][0].right == R);

    // Tape 3 (AfterRead): bit-identical even though processor is enabled.
    CHECK (inner.received[3][0].left  == L);
    CHECK (inner.received[3][0].right == R);

    // Tape 2 (BeforeWrite + enabled): some sample must have moved.
    const auto& g2 = inner.received[2][0];
    bool moved = false;
    for (int i = 0; i < kBlockSize; ++i)
        if (g2.left [static_cast<std::size_t> (i)] != L[static_cast<std::size_t> (i)]
         || g2.right[static_cast<std::size_t> (i)] != R[static_cast<std::size_t> (i)])
        {
            moved = true;
            break;
        }
    CHECK (moved);
}

TEST_CASE ("TapeColoringSink ignores ops on an unknown tape (no crash, no mode tracking)",
           "[tape-coloring-sink][tapecolor]")
{
    // Misordered calls (setMode before addTape, removeTape on an absent
    // tape) must be no-ops. Audio thread must never see an inconsistent
    // intermediate state — and the inner sink keeps forwarding.
    RecordingTapeSink inner;
    TapeColoringSink  sink (&inner, kSampleRate, kBlockSize);

    sink.setMode    (TapeId (42), TapeColorMode::BeforeWrite); // no-op
    sink.removeTape (TapeId (42));                             // no-op
    CHECK (sink.modeFor (TapeId (42)) == TapeColorMode::None); // unknown -> None

    auto L = rampBlock (0.0f, 0.001f, kBlockSize);
    auto R = rampBlock (0.0f, 0.002f, kBlockSize);
    sink.deliverTapeBlock (TapeId (42), L.data(), R.data(), kBlockSize);
    REQUIRE (inner.received[42].size() == 1u);
    CHECK   (inner.received[42][0].left  == L);
    CHECK   (inner.received[42][0].right == R);
}
