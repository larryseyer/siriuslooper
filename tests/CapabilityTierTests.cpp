// Tests for the startup capability assessment (white paper Part XIII). The
// assessment is the one place the system decides, on the performer's behalf,
// how much fidelity the hardware can sustain — so these tests pin down *why*
// each tier is chosen: a workstation earns Lavish, every hardware constraint
// that means "this rating will not be delivered reliably" demotes the result,
// and the ladder always has Survival as a floor so the system always runs.
#include "CapabilityTier.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using ida::AsrcQuality;
using ida::CapabilityTier;
using ida::EffectStrategy;
using ida::HardwareProfile;
using ida::TapeFormat;

namespace
{
    constexpr std::int64_t gib (std::int64_t n) { return n * 1024 * 1024 * 1024; }
    constexpr std::int64_t mib (std::int64_t n) { return n * 1024 * 1024; }

    /// A high-end workstation: comfortably clears every Lavish threshold.
    HardwareProfile workstation()
    {
        HardwareProfile hw;
        hw.cpuCores = 16;
        hw.hasVectorUnit = true;
        hw.ramTotalBytes = gib (64);
        hw.ramAvailableBytes = gib (48);
        hw.storageWriteBytesPerSec = mib (2000);
        hw.audioBufferFrames = 128;
        hw.onBattery = false;
        hw.thermallyThrottled = false;
        return hw;
    }
}

TEST_CASE ("a high-end workstation earns the Lavish tier", "[capability]")
{
    CHECK (ida::selectTier (workstation()) == CapabilityTier::Lavish);
}

TEST_CASE ("running on battery rules out Lavish", "[capability]")
{
    // White paper Part 13.2: Lavish is a workstation-on-mains profile; battery
    // is the Tight tier's stated domain. Even a workstation's spec, unplugged,
    // cannot be trusted to sustain Lavish.
    HardwareProfile hw = workstation();
    hw.onBattery = true;
    CHECK (ida::selectTier (hw) == CapabilityTier::Comfortable);
}

TEST_CASE ("a missing vector unit caps the tier at Tight", "[capability]")
{
    // The top two tiers assume an all-live effect strategy; without SIMD the
    // per-sample DSP budget cannot sustain it, regardless of core count.
    HardwareProfile hw = workstation();
    hw.hasVectorUnit = false;
    CHECK (ida::selectTier (hw) == CapabilityTier::Tight);
}

TEST_CASE ("thermal throttling demotes one step from the measured base", "[capability]")
{
    // Trust the measured thermal posture over the nameplate rating: a throttled
    // workstation is, right now, not a workstation.
    HardwareProfile hw = workstation();
    hw.thermallyThrottled = true;
    CHECK (ida::selectTier (hw) == CapabilityTier::Comfortable);
}

TEST_CASE ("a large audio buffer is treated as a glitch-risk signal", "[capability]")
{
    // A device that can only run at a big buffer is already conceding it cannot
    // keep up tight — and rule 1 is audio-never-glitches, so step down.
    HardwareProfile hw = workstation();
    hw.audioBufferFrames = 1024;
    CHECK (ida::selectTier (hw) == CapabilityTier::Comfortable);
}

TEST_CASE ("constraints compound — battery plus throttling stack", "[capability]")
{
    HardwareProfile hw = workstation();
    hw.onBattery = true;          // caps at Comfortable
    hw.thermallyThrottled = true; // then one step down
    CHECK (ida::selectTier (hw) == CapabilityTier::Tight);
}

TEST_CASE ("a modern laptop on AC lands at Comfortable", "[capability]")
{
    HardwareProfile hw;
    hw.cpuCores = 8;
    hw.hasVectorUnit = true;
    hw.ramTotalBytes = gib (16);
    hw.ramAvailableBytes = gib (10); // clears Comfortable, short of Lavish's 16
    hw.storageWriteBytesPerSec = mib (1500);
    hw.audioBufferFrames = 256;
    CHECK (ida::selectTier (hw) == CapabilityTier::Comfortable);
}

TEST_CASE ("marginal hardware falls to Survival, never below", "[capability]")
{
    // White paper Part 13.4: the system always runs. Survival is the floor, and
    // every demotion clamps to it.
    HardwareProfile hw;
    hw.cpuCores = 1;
    hw.hasVectorUnit = false;
    hw.ramTotalBytes = gib (1);
    hw.ramAvailableBytes = mib (512);
    hw.storageWriteBytesPerSec = mib (20);
    hw.audioBufferFrames = 2048;
    hw.onBattery = true;
    hw.thermallyThrottled = true;
    CHECK (ida::selectTier (hw) == CapabilityTier::Survival);
}

TEST_CASE ("each tier's policy matches the white paper Part 13.2 table", "[capability]")
{
    const auto lavish = ida::policyFor (CapabilityTier::Lavish);
    CHECK (lavish.tapeFormat == TapeFormat::UncompressedPcm);
    CHECK (lavish.asrcQuality == AsrcQuality::VeryHigh);
    CHECK (lavish.effectStrategy == EffectStrategy::AllLive);
    CHECK (lavish.ringDepthSeconds > ida::policyFor (CapabilityTier::Comfortable).ringDepthSeconds);
    CHECK (lavish.flushIntervalMs == 1);

    const auto comfortable = ida::policyFor (CapabilityTier::Comfortable);
    CHECK (comfortable.tapeFormat == TapeFormat::Flac);
    CHECK (comfortable.asrcQuality == AsrcQuality::VeryHigh);
    CHECK (comfortable.effectStrategy == EffectStrategy::AllLive);
    CHECK (comfortable.flushIntervalMs == 50);

    const auto tight = ida::policyFor (CapabilityTier::Tight);
    CHECK (tight.asrcQuality == AsrcQuality::High);
    CHECK (tight.effectStrategy == EffectStrategy::MixedLiveCached);
    CHECK (tight.flushIntervalMs == 200);

    const auto survival = ida::policyFor (CapabilityTier::Survival);
    CHECK (survival.tapeFormat == TapeFormat::Flac);
    CHECK (survival.asrcQuality == AsrcQuality::Medium);
    CHECK (survival.effectStrategy == EffectStrategy::AggressiveCaching);
    CHECK (survival.ringDepthSeconds >= 1); // the ring is never empty
    CHECK (survival.flushIntervalMs == 1000);
}

TEST_CASE ("every tier has a stable display name", "[capability]")
{
    CHECK (std::string (ida::toString (CapabilityTier::Lavish)) == "Lavish");
    CHECK (std::string (ida::toString (CapabilityTier::Comfortable)) == "Comfortable");
    CHECK (std::string (ida::toString (CapabilityTier::Tight)) == "Tight");
    CHECK (std::string (ida::toString (CapabilityTier::Survival)) == "Survival");
}
