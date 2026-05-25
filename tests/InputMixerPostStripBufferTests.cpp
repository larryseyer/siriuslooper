// V9 conformance — Slice 2: the per-channel post-strip buffer seam.
//
// InputMixer exposes its per-channel post-strip (post-ChannelStrip) stereo
// output as a stable pointer that Slice 3 hands to
// OutputMixer::setChannelAudioSource(...) when MON goes on. The contract
// this test pins:
//   * `postStripPointer(id, 0|1)` is non-null while the channel exists,
//     null for unknown ids and out-of-range sides.
//   * The pointer is stable across renderInputGraph calls — the seam's
//     whole point is that OutputMixer caches it once and reads in-place
//     on every audio block.
//   * Removing the channel frees the storage; subsequent lookups return
//     null (no use-after-free on the seam).
//
// `InputMixer` has no `prepare(sr, blockSize, maxChannels)` API in the
// current shape — storage is sized to the engine-private `kMaxScratchSamples`
// ceiling at construction. The test therefore drives `addChannel(InputId,
// SignalType)` directly, matching the actual public surface.

#include "ida/Channel.h"
#include "ida/InputMixer.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

TEST_CASE ("InputMixer post-strip buffer lifecycle", "[input-mixer][post-strip]")
{
    using ida::ChannelId;
    using ida::InputId;
    using ida::InputMixer;
    using ida::SignalType;

    InputMixer mixer;
    const auto chId = mixer.addChannel (InputId (0), SignalType::Audio);

    SECTION ("pointer is non-null while channel exists")
    {
        REQUIRE (mixer.postStripPointer (chId, 0) != nullptr);
        REQUIRE (mixer.postStripPointer (chId, 1) != nullptr);
    }

    SECTION ("pointer is null for an out-of-range side")
    {
        REQUIRE (mixer.postStripPointer (chId, -1) == nullptr);
        REQUIRE (mixer.postStripPointer (chId, 2)  == nullptr);
    }

    SECTION ("pointer is null for an unknown channel id")
    {
        REQUIRE (mixer.postStripPointer (ChannelId (9999), 0) == nullptr);
    }

    SECTION ("pointer becomes null after channel removal")
    {
        mixer.removeChannel (chId);
        REQUIRE (mixer.postStripPointer (chId, 0) == nullptr);
        REQUIRE (mixer.postStripPointer (chId, 1) == nullptr);
    }

    SECTION ("pointer is stable across renderInputGraph calls")
    {
        const float* p0L = mixer.postStripPointer (chId, 0);
        const float* p0R = mixer.postStripPointer (chId, 1);
        REQUIRE (p0L != nullptr);
        REQUIRE (p0R != nullptr);

        // Wire the channel to device input 0/1 so renderInputGraph actually
        // touches the strip; the pointer must remain identical block-to-block.
        mixer.setChannelInputSource (chId, 0, 1, /*stereo=*/true);

        std::array<float, 256> dummyIn {};
        const float* inputs[2] { dummyIn.data(), dummyIn.data() };
        mixer.renderInputGraph (inputs, 2, nullptr, 0, 256);

        REQUIRE (mixer.postStripPointer (chId, 0) == p0L);
        REQUIRE (mixer.postStripPointer (chId, 1) == p0R);

        // And a second block — still the same address.
        mixer.renderInputGraph (inputs, 2, nullptr, 0, 256);
        REQUIRE (mixer.postStripPointer (chId, 0) == p0L);
        REQUIRE (mixer.postStripPointer (chId, 1) == p0R);
    }
}

TEST_CASE ("InputMixer post-strip buffer reflects strip output after renderInputGraph",
           "[input-mixer][post-strip]")
{
    using ida::ChannelId;
    using ida::InputId;
    using ida::InputMixer;
    using ida::SignalType;

    InputMixer mixer;
    const auto chId = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelInputSource (chId, 0, 1, /*stereo=*/true);

    // Drive a non-zero device input through the strip; the strip's default
    // gain is unity (no mute), so the post-strip buffer must carry the
    // signal verbatim — Slice 3 will hand this pointer to OutputMixer and
    // the seam only works if the writes actually happen.
    std::array<float, 256> left {}, right {};
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i]  = 0.25f;
        right[i] = -0.25f;
    }
    const float* inputs[2] { left.data(), right.data() };
    mixer.renderInputGraph (inputs, 2, nullptr, 0, 256);

    const float* postL = mixer.postStripPointer (chId, 0);
    const float* postR = mixer.postStripPointer (chId, 1);
    REQUIRE (postL != nullptr);
    REQUIRE (postR != nullptr);
    REQUIRE (postL[0]   == 0.25f);
    REQUIRE (postL[128] == 0.25f);
    REQUIRE (postR[0]   == -0.25f);
    REQUIRE (postR[128] == -0.25f);
}
