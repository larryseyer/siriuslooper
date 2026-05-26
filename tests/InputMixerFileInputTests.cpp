#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ida/InputMixer.h"
#include "ida/IFileInputSourceRegistry.h"
#include "ida/Channel.h"
#include "ida/InputDescriptor.h"

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{

/// Minimal stub registry: returns a callable that fills L/R with a known
/// constant per InputId. Used to verify InputMixer correctly dispatches
/// file-input channels through the cached callable without ever touching
/// the registry on the audio thread.
class StubFileInputRegistry : public ida::IFileInputSourceRegistry
{
public:
    struct Pattern
    {
        float left  { 0.0f };
        float right { 0.0f };
    };

    void seed (ida::InputId id, Pattern p) { patterns_[id.value()] = p; }

    ida::FileInputPullCallable resolveFileInputPull (ida::InputId id) noexcept override
    {
        auto it = patterns_.find (id.value());
        if (it == patterns_.end()) return {};
        return ida::FileInputPullCallable { &StubFileInputRegistry::pullStatic,
                                            &it->second };
    }

private:
    static bool pullStatic (void* userdata, float* L, float* R, int n) noexcept
    {
        const auto* p = static_cast<const Pattern*> (userdata);
        for (int i = 0; i < n; ++i) { L[i] = p->left; R[i] = p->right; }
        return true;
    }

    std::unordered_map<std::int64_t, Pattern> patterns_;
};

/// Togglable stub. When `returnFalse_` is true, the pull function writes
/// a sentinel pattern AND returns false (engine must silence the scratch).
/// When false, writes a known pattern and returns true (engine renders it).
/// Lets the test verify both the silencing path AND that the engine
/// doesn't latch into silence after one false return.
class StubReturnsFalseRegistry : public ida::IFileInputSourceRegistry
{
public:
    void setReturnFalse (bool b) noexcept { returnFalse_ = b; }

    ida::FileInputPullCallable resolveFileInputPull (ida::InputId /*id*/) noexcept override
    {
        return ida::FileInputPullCallable { &pullStatic, this };
    }

private:
    static bool pullStatic (void* userdata, float* L, float* R, int n) noexcept
    {
        auto* self = static_cast<StubReturnsFalseRegistry*> (userdata);
        if (self->returnFalse_)
        {
            // Sentinel: write 9999 and return false. Engine must silence.
            for (int i = 0; i < n; ++i) { L[i] = 9999.0f; R[i] = 9999.0f; }
            return false;
        }
        // Recovery: write known signal and return true.
        for (int i = 0; i < n; ++i) { L[i] = 0.11f; R[i] = -0.22f; }
        return true;
    }

    bool returnFalse_ { true };   // start in the false-return mode
};

} // namespace

TEST_CASE ("InputMixer renders a file-input channel through its cached pull callable",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId = ida::kFileInputIdBase;  // 100000
    stub.seed (fileId, { 0.25f, -0.75f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    // Match the existing test pattern (see InputMixerTests.cpp's
    // exportGraphState test): registerInput with a minimal descriptor before
    // addChannel. Tape id 1 is the always-present primary tape.
    ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio, std::string ("FileInput 1"),
        std::optional<int> {}
    };
    mixer.registerInput (fileId, desc);

    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 128;
    std::array<float, numFrames> dummyDeviceL {};
    std::array<float, numFrames> dummyDeviceR {};
    const float* deviceIn[2] = { dummyDeviceL.data(), dummyDeviceR.data() };

    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    REQUIRE (postL != nullptr);
    REQUIRE (postR != nullptr);

    // The strip applies equal-power pan at center (~0.7071 attenuation per side
    // at unity gain). Assert the post-strip signal matches the stub's pattern
    // scaled by that law — proves the file-input callable supplied the data
    // (not deviceIn, which is silent) and the strip ran over it.
    constexpr float kPanGain = 0.70710677f; // cos(pi/4) == sin(pi/4)
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx ( 0.25f * kPanGain).margin (1e-5f));
        REQUIRE (postR[n] == Catch::Approx (-0.75f * kPanGain).margin (1e-5f));
    }
}

TEST_CASE ("InputMixer renders file-input channels even when deviceIn is null",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };
    stub.seed (fileId, { 0.10f, -0.10f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio,
        std::string ("FileInput null-device"), std::optional<int> {}
    };
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 64;
    mixer.renderInputGraph (nullptr, 0, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    REQUIRE (postL != nullptr);
    REQUIRE (postR != nullptr);

    // Same pan-law adjustment as the primary test — proves the file-input
    // callable supplied the data even with no device buffers.
    constexpr float kPanGain = 0.70710677f;
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx ( 0.10f * kPanGain).margin (1e-5f));
        REQUIRE (postR[n] == Catch::Approx (-0.10f * kPanGain).margin (1e-5f));
    }
}

TEST_CASE ("InputMixer file-input channel renders silence when no registry is set",
           "[file-input][input-mixer]")
{
    ida::InputMixer mixer;
    // Deliberately skip setFileInputSourceRegistry.

    const ida::InputId fileId { ida::kFileInputIdBase };
    ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio,
        std::string ("FileInput no-registry"), std::optional<int> {}
    };
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);   // no-op (registry null)

    constexpr int numFrames = 64;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    REQUIRE (postL != nullptr);
    REQUIRE (postR != nullptr);
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx (0.0f));
        REQUIRE (postR[n] == Catch::Approx (0.0f));
    }
}

TEST_CASE ("InputMixer file-input channel renders silence when registry doesn't know the id",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    // Do NOT seed any patterns.

    // InputId is explicit-construction-only; build via .value() arithmetic.
    const ida::InputId fileId { ida::kFileInputIdBase.value() + 5 };
    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio,
        std::string ("FileInput unknown"), std::optional<int> {}
    };
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);   // resolves invalid

    constexpr int numFrames = 64;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);

    const float* postL = mixer.postStripPointer (channelId, 0);
    const float* postR = mixer.postStripPointer (channelId, 1);
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (postL[n] == Catch::Approx (0.0f));
        REQUIRE (postR[n] == Catch::Approx (0.0f));
    }
}

TEST_CASE ("InputMixer renders device and file channels correctly in one render call",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };
    stub.seed (fileId, { 0.10f, 0.20f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    // Device channel — InputId 0 on device channel index 0.
    const ida::InputId deviceId { 0 };
    ida::InputDescriptor devDesc {
        ida::TapeId (1), ida::InputKind::Audio,
        std::string ("Device 1"), std::optional<int> {}
    };
    mixer.registerInput (deviceId, devDesc);
    const auto devChannel = mixer.addChannel (deviceId, ida::SignalType::Audio);
    mixer.setChannelInputSource (devChannel, 0, 0, /*stereo=*/false);

    // File channel — TapeId in descriptor is just back-reference metadata,
    // not a routing destination, so referencing tape 2 without addTape() is fine.
    ida::InputDescriptor fileDesc {
        ida::TapeId (2), ida::InputKind::Audio,
        std::string ("FileInput mixed"), std::optional<int> {}
    };
    mixer.registerInput (fileId, fileDesc);
    const auto fileChannel = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (fileChannel, fileId);

    constexpr int numFrames = 32;
    std::array<float, numFrames> devL;
    devL.fill (0.40f);
    const float* deviceIn[1] = { devL.data() };

    mixer.renderInputGraph (deviceIn, 1, nullptr, 0, numFrames);

    const float* devPostL  = mixer.postStripPointer (devChannel,  0);
    const float* filePostL = mixer.postStripPointer (fileChannel, 0);
    const float* filePostR = mixer.postStripPointer (fileChannel, 1);

    constexpr float kPanGain = 0.70710677f;
    for (int n = 0; n < numFrames; ++n)
    {
        REQUIRE (devPostL[n]  == Catch::Approx (0.40f * kPanGain).margin (1e-5f));
        REQUIRE (filePostL[n] == Catch::Approx (0.10f * kPanGain).margin (1e-5f));
        REQUIRE (filePostR[n] == Catch::Approx (0.20f * kPanGain).margin (1e-5f));
    }
}

TEST_CASE ("InputMixer file-input channel renders consistently across consecutive blocks",
           "[file-input][input-mixer]")
{
    StubFileInputRegistry stub;
    const ida::InputId fileId { ida::kFileInputIdBase };
    stub.seed (fileId, { 0.33f, 0.66f });

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio,
        std::string ("FileInput multi-call"), std::optional<int> {}
    };
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 64;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };

    constexpr float kPanGain = 0.70710677f;
    for (int block = 0; block < 3; ++block)
    {
        mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);
        const float* postL = mixer.postStripPointer (channelId, 0);
        const float* postR = mixer.postStripPointer (channelId, 1);
        for (int n = 0; n < numFrames; ++n)
        {
            REQUIRE (postL[n] == Catch::Approx (0.33f * kPanGain).margin (1e-5f));
            REQUIRE (postR[n] == Catch::Approx (0.66f * kPanGain).margin (1e-5f));
        }
    }
}

TEST_CASE ("InputMixer silences a file-input channel when the pull callable returns false, "
           "then recovers when it returns true on the next block",
           "[file-input][input-mixer]")
{
    StubReturnsFalseRegistry stub;   // starts with returnFalse_ = true
    const ida::InputId fileId { ida::kFileInputIdBase };

    ida::InputMixer mixer;
    mixer.setFileInputSourceRegistry (&stub);

    ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio,
        std::string ("FileInput false-return"), std::optional<int> {}
    };
    mixer.registerInput (fileId, desc);
    const auto channelId = mixer.addChannel (fileId, ida::SignalType::Audio);
    mixer.setChannelFileInputSource (channelId, fileId);

    constexpr int numFrames = 32;
    std::array<float, numFrames> z {};
    const float* deviceIn[2] = { z.data(), z.data() };

    // Block 1: stub returns false → engine silences the 9999 sentinel.
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);
    {
        const float* postL = mixer.postStripPointer (channelId, 0);
        const float* postR = mixer.postStripPointer (channelId, 1);
        for (int n = 0; n < numFrames; ++n)
        {
            REQUIRE (postL[n] == Catch::Approx (0.0f));
            REQUIRE (postR[n] == Catch::Approx (0.0f));
        }
    }

    // Block 2: flip the toggle. Engine must NOT have latched into silence;
    // the second block should reflect the stub's pattern (scaled by the
    // strip's equal-power center-pan gain).
    stub.setReturnFalse (false);
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, numFrames);
    {
        const float* postL = mixer.postStripPointer (channelId, 0);
        const float* postR = mixer.postStripPointer (channelId, 1);
        constexpr float kPanGain = 0.70710677f;
        for (int n = 0; n < numFrames; ++n)
        {
            REQUIRE (postL[n] == Catch::Approx ( 0.11f * kPanGain).margin (1e-5f));
            REQUIRE (postR[n] == Catch::Approx (-0.22f * kPanGain).margin (1e-5f));
        }
    }
}
