#pragma once

#include <cstdint>

namespace ida
{

/// IDA-side EQ parameter surface — operator-facing struct that mirrors
/// the subset of `otto::effects::PlayerEffectsConfig` the EQ tab
/// edits. JUCE-free, OTTO-free; lives in `core/` so the UI layer and
/// the host's typed accessors share one type without taking on the
/// OTTO include path. `EqAdapter` (engine, OTTO-aware) is the one
/// translation site — its `setEqConfig` / `eqConfig` methods map
/// between this struct and the full `PlayerEffectsConfig`.
///
/// Slope values match OTTO's `FilterSlope` enum ordinals (12 dB/oct
/// = 1-stage Butterworth, 24 dB/oct = 2-stage). UI exposes a single
/// toggle so the wire value is just the integer.
///
/// Defaults are PlayerEQ's flat-response defaults so an EqConfig{}
/// reads back the same shape a freshly-constructed adapter publishes.
struct EqConfig
{
    bool enabled = true;

    // High-pass
    float    hpFreq = 20.0f;            ///< 20-500 Hz (20 = bypass)
    std::uint8_t hpSlopeDbPerOct = 12;  ///< 12 or 24

    // Low shelf
    float lowGain = 0.0f;               ///< -12..+12 dB
    float lowFreq = 100.0f;             ///< 40-500 Hz
    float lowQ    = 1.0f;               ///< 0.1-10

    // Mid parametric
    float midGain = 0.0f;
    float midFreq = 1000.0f;
    float midQ    = 1.0f;

    // High shelf
    float highGain = 0.0f;
    float highFreq = 8000.0f;
    float highQ    = 1.0f;

    // Low-pass
    float    lpFreq = 20000.0f;         ///< 2000-20000 Hz (20000 = bypass)
    std::uint8_t lpSlopeDbPerOct = 12;
};

/// IDA-side compressor parameter surface — mirrors the comp* fields
/// of `otto::effects::PlayerEffectsConfig`. Same translation-site
/// pattern as `EqConfig`: `CmpAdapter` (engine) is the only place
/// that bridges this and OTTO's full struct.
///
/// Defaults are PlayerCompressor's defaults (the same values
/// PlayerEffectsConfig::resetToDefaults installs for the comp block).
struct CmpConfig
{
    bool  enabled       = true;
    float threshold     = -12.0f;       ///< -60..0 dB
    float ratio         = 4.0f;         ///< 1..20
    float attackMs      = 10.0f;        ///< 0.1..100 ms
    float releaseMs     = 100.0f;       ///< 10..1000 ms
    float makeupDb      = 0.0f;         ///< 0..24 dB
    float mix           = 1.0f;         ///< 0..1 parallel mix
    bool  sidechainHpf  = true;
};

} // namespace ida
