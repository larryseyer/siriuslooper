// 2026-05-24 — pins the whitepaper §6 / §7 rule that the Output Mixer's
// phrase channels are silent unless the transport is running and a
// Constituent renderer / tape playback is producing audio for them.
// They MUST NOT pipe live device input to the master.
//
// Before this slice, OutputMixer::renderBuffer Step 1 carried an "M5 proxy"
// that copied inputChannelData[id-1] into the channel scratch as a
// placeholder for Constituent rendering. The placeholder never got
// replaced, and it leaked live input straight through the master bus —
// independent of the Input Mixer's per-channel MON button (the only
// sanctioned input→output path per whitepaper V9 §5.2 / §7.2).
//
// The fix removes the proxy; phrase channels now stay silent until a real
// audio source feeds them. Until that source path lands (Constituent
// renderer / tape playback slice), the master bus must read zero peak
// even when live input is present.
#include "ida/Bus.h"
#include "ida/ChannelStrip.h"
#include "ida/OutputMixer.h"
#include "ida/SignalType.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using ida::BusId;
using ida::ChannelStrip;
using ida::OutputMixer;
using ida::SignalType;

TEST_CASE ("OutputMixer phrase channels do not pipe live input to master",
           "[output-mixer][monitor][phrase-silence]")
{
    // Reproduces the reported bug: with a phrase-style channel registered
    // and live input present at the device, the master bus must read zero
    // — the Input Mixer's MON button is the only sanctioned input→output
    // path (V9 §7.2; via an auto-created OutputMixer channel reading the
    // input's post-strip buffer).
    OutputMixer mixer;
    const auto ch = mixer.addChannel (SignalType::Audio);
    mixer.setChannelStrip (ch, std::make_unique<ChannelStrip<SignalType::Audio>> ());

    constexpr int kFrames = 64;
    std::array<float, kFrames> liveIn;  liveIn.fill (1.0f);
    const float* inputs[1] = { liveIn.data() };

    std::array<float, kFrames> outLeft;  outLeft.fill  (0.0f);
    std::array<float, kFrames> outRight; outRight.fill (0.0f);
    float* outputs[2] = { outLeft.data(), outRight.data() };

    mixer.renderBuffer (inputs, 1, outputs, 2, kFrames);

    auto* master = mixer.busForId (BusId { 0 });
    REQUIRE (master != nullptr);
    CHECK (master->peakLeft()  == Catch::Approx (0.0f));
    CHECK (master->peakRight() == Catch::Approx (0.0f));

    // Output buffers must also remain at the caller's zero-fill — the
    // master's terminal write would otherwise leave residue.
    for (float v : outLeft)  CHECK (v == Catch::Approx (0.0f));
    for (float v : outRight) CHECK (v == Catch::Approx (0.0f));
}
