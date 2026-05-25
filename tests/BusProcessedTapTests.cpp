// Tests the seam V9 §7.2's per-bus MON path reads from: a const accessor on
// `Bus` exposing the bus's processed (post-processing) stereo output buffer.
// Mirror of the per-channel `InputMixer::postStripPointer` seam, but on the
// bus side. No allocation, no DSP work — just exposes `Bus::processedBuffer_`
// at a stable address for the bus's lifetime so `OutputMixer::setChannelAudioSource`
// can pin to it once.

#include "ida/Bus.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Bus::postProcessingPointer exposes processedBuffer_ for L and R, nullptr for OOB",
           "[bus][monitor-tap]")
{
    ida::Bus bus { ida::BusId { 7 }, ida::BusConfig { 2, "TapBus" } };

    const auto* l = bus.postProcessingPointer (0);
    const auto* r = bus.postProcessingPointer (1);
    REQUIRE (l != nullptr);
    REQUIRE (r != nullptr);
    CHECK (l != r);                                  // L and R live in distinct regions
    CHECK (r == l + ida::Bus::kMaxBusMixSamples);    // channel-major stride

    CHECK (bus.postProcessingPointer (-1) == nullptr);
    CHECK (bus.postProcessingPointer (2)  == nullptr);

    // Pointer is stable across repeated reads (no allocation on access).
    CHECK (bus.postProcessingPointer (0) == l);
    CHECK (bus.postProcessingPointer (1) == r);
}
