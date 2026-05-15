#include "CapabilityTier.h"

namespace sirius
{

namespace
{
    constexpr std::int64_t gibibytes (std::int64_t count) { return count * 1024 * 1024 * 1024; }
    constexpr std::int64_t mebibytes (std::int64_t count) { return count * 1024 * 1024; }

    /// Tiers are ordered best-to-worst by enum value, so "worse" is the
    /// numerically larger one and demotion is a max toward Survival.
    constexpr int tierIndex (CapabilityTier tier) { return static_cast<int> (tier); }

    CapabilityTier fromIndex (int index)
    {
        if (index >= tierIndex (CapabilityTier::Survival))
            return CapabilityTier::Survival;
        if (index <= tierIndex (CapabilityTier::Lavish))
            return CapabilityTier::Lavish;
        return static_cast<CapabilityTier> (index);
    }

    /// Demotes `tier` so it is no better than `floor` — used when a single
    /// constraint rules out every tier above a certain point.
    CapabilityTier noBetterThan (CapabilityTier tier, CapabilityTier floor)
    {
        return tierIndex (tier) > tierIndex (floor) ? tier : floor;
    }

    CapabilityTier oneStepDown (CapabilityTier tier)
    {
        return fromIndex (tierIndex (tier) + 1);
    }

    /// The optimistic starting point: the best tier raw compute, memory
    /// headroom, and storage throughput alone would allow.
    CapabilityTier baseTier (const HardwareProfile& hw)
    {
        if (hw.cpuCores >= 8 && hw.ramAvailableBytes >= gibibytes (16)
            && hw.storageWriteBytesPerSec >= mebibytes (500))
            return CapabilityTier::Lavish;

        if (hw.cpuCores >= 4 && hw.ramAvailableBytes >= gibibytes (8))
            return CapabilityTier::Comfortable;

        if (hw.cpuCores >= 2 && hw.ramAvailableBytes >= gibibytes (2))
            return CapabilityTier::Tight;

        return CapabilityTier::Survival;
    }
}

CapabilityTier selectTier (const HardwareProfile& hardware)
{
    CapabilityTier tier = baseTier (hardware);

    // No vector unit: per-sample DSP throughput cannot sustain the live-effect
    // strategy the top two tiers assume.
    if (! hardware.hasVectorUnit)
        tier = noBetterThan (tier, CapabilityTier::Tight);

    // Lavish is a workstation-on-mains profile by definition; sustained draw on
    // battery is exactly what the Tight tier exists for.
    if (hardware.onBattery)
        tier = noBetterThan (tier, CapabilityTier::Comfortable);

    // Thermal throttling means the hardware is already not delivering its
    // nameplate rating — trust the measured posture, not the spec sheet.
    if (hardware.thermallyThrottled)
        tier = oneStepDown (tier);

    // A large device buffer is the audio stack already conceding it cannot keep
    // up at a tight buffer — a glitch-risk signal, and rule 1 is audio first.
    if (hardware.audioBufferFrames >= 1024)
        tier = oneStepDown (tier);

    return tier;
}

TierPolicy policyFor (CapabilityTier tier)
{
    switch (tier)
    {
        case CapabilityTier::Lavish:
            return { TapeFormat::UncompressedPcm, AsrcQuality::VeryHigh,
                     EffectStrategy::AllLive, 120 };
        case CapabilityTier::Comfortable:
            return { TapeFormat::Flac, AsrcQuality::VeryHigh,
                     EffectStrategy::AllLive, 30 };
        case CapabilityTier::Tight:
            return { TapeFormat::Flac, AsrcQuality::High,
                     EffectStrategy::MixedLiveCached, 5 };
        case CapabilityTier::Survival:
            return { TapeFormat::Flac, AsrcQuality::Medium,
                     EffectStrategy::AggressiveCaching, 1 };
    }

    // Unreachable: every enumerator is handled above.
    return { TapeFormat::Flac, AsrcQuality::Medium, EffectStrategy::AggressiveCaching, 1 };
}

const char* toString (CapabilityTier tier)
{
    switch (tier)
    {
        case CapabilityTier::Lavish:      return "Lavish";
        case CapabilityTier::Comfortable: return "Comfortable";
        case CapabilityTier::Tight:       return "Tight";
        case CapabilityTier::Survival:    return "Survival";
    }
    return "Survival";
}

} // namespace sirius
