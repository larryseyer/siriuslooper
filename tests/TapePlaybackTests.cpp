#include "ida/TransportPlayhead.h"
#include "ida/ActiveReadsSnapshot.h"
#include "ida/Bus.h"
#include "ida/ChannelStrip.h"
#include "ida/OutputMixer.h"
#include "ida/SignalType.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace ida;

TEST_CASE ("advancePlayedSamples advances only while playing", "[tape-playback][playhead]")
{
    std::int64_t pos = 0;
    pos = advancePlayedSamples (pos, 512, /*isPlaying=*/true);
    REQUIRE (pos == 512);
    pos = advancePlayedSamples (pos, 512, /*isPlaying=*/false); // stopped: holds
    REQUIRE (pos == 512);
    pos = advancePlayedSamples (pos, 256, /*isPlaying=*/true);
    REQUIRE (pos == 768);
}

TEST_CASE ("advancePlayedSamples ignores non-positive blocks", "[tape-playback][playhead]")
{
    REQUIRE (advancePlayedSamples (100, 0,   true) == 100);
    REQUIRE (advancePlayedSamples (100, -8,  true) == 100);
}

TEST_CASE ("playheadSeconds is identity-calibrated", "[tape-playback][playhead]")
{
    REQUIRE (playheadSeconds (48000, 48000.0) == Catch::Approx (1.0));
    REQUIRE (playheadSeconds (0, 48000.0) == Catch::Approx (0.0));
    REQUIRE (playheadSeconds (24000, 0.0) == Catch::Approx (0.0)); // sr==0 guard
}

TEST_CASE ("ActiveReadsPublisher round-trips a snapshot", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;

    ActiveReadsSnapshot in;
    in.add ({ /*slot=*/3, /*tapeSampleStart=*/1000, /*active=*/true });
    in.add ({ /*slot=*/5, /*tapeSampleStart=*/2048, /*active=*/true });
    pub.publish (in);

    ActiveReadsSnapshot out;
    pub.read (out);                       // lock-free consumer read
    REQUIRE (out.count == 2);
    REQUIRE (out.slots[0].slot == 3);
    REQUIRE (out.slots[0].tapeSampleStart == 1000);
    REQUIRE (out.slots[1].slot == 5);
    REQUIRE (out.slots[1].active);
}

TEST_CASE ("ActiveReadsSnapshot clamps to capacity", "[tape-playback][snapshot]")
{
    ActiveReadsSnapshot s;
    for (int i = 0; i < kMaxPhraseSlots + 10; ++i)
        s.add ({ i, /*tapeSampleStart=*/0, /*active=*/true });
    REQUIRE (s.count == kMaxPhraseSlots);     // never overruns the fixed array
}

TEST_CASE ("publisher read before any publish yields empty", "[tape-playback][snapshot]")
{
    ActiveReadsPublisher pub;
    ActiveReadsSnapshot out;
    pub.read (out);
    REQUIRE (out.count == 0);
}

TEST_CASE ("phrase scratch pointer is stable and zero-initialized",
           "[tape-playback][phrase-scratch]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.ensurePhraseScratch (ch);

    const float* l0 = mixer.phraseScratchPointer (ch, 0);
    const float* r0 = mixer.phraseScratchPointer (ch, 1);
    REQUIRE (l0 != nullptr);
    REQUIRE (r0 != nullptr);
    REQUIRE (l0 != r0);
    REQUIRE (l0[0] == 0.0f);

    // Pointer must not move across a second ensure call (stable for lifetime).
    mixer.ensurePhraseScratch (ch);
    REQUIRE (mixer.phraseScratchPointer (ch, 0) == l0);

    // Out-of-range side and unknown channel return nullptr.
    REQUIRE (mixer.phraseScratchPointer (ch, 2) == nullptr);
    REQUIRE (mixer.phraseScratchPointer (OutputChannelId { 9999 }, 0) == nullptr);
}

TEST_CASE ("writing phrase scratch then rendering produces non-silent output",
           "[tape-playback][phrase-scratch]")
{
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.setChannelStrip (ch, std::make_unique<ChannelStrip<SignalType::Audio>> ());
    mixer.ensurePhraseScratch (ch);
    mixer.setChannelAudioSource (ch,
                                 mixer.phraseScratchPointer (ch, 0),
                                 mixer.phraseScratchPointer (ch, 1));

    constexpr int n = 64;
    float* l = mixer.mutablePhraseScratch (ch, 0);
    float* r = mixer.mutablePhraseScratch (ch, 1);
    for (int i = 0; i < n; ++i) { l[i] = 0.5f; r[i] = 0.5f; }

    std::array<float, n> outLeft;  outLeft.fill  (0.0f);
    std::array<float, n> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    // No live input needed — phrase channels read from their audio source.
    mixer.renderBuffer (nullptr, 0, outputs, 2, n);

    REQUIRE (std::abs (outLeft[0]) > 0.0f);
}
