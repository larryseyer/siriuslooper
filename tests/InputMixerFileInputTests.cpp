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
