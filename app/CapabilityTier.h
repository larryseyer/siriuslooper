#pragma once

#include <cstdint>

namespace ida
{

/// One of the four operating profiles the system selects at session startup
/// (white paper Part XIII). The tier is locked for the session's lifetime: the
/// performer authors musical intent, the system chooses fidelity once, against
/// the hardware it actually finds itself running on.
enum class CapabilityTier
{
    Lavish,      ///< high-end workstation
    Comfortable, ///< modern laptop on AC
    Tight,       ///< older hardware, or running on battery
    Survival     ///< marginal hardware
};

/// The tape storage format a tier uses (white paper Parts 6.5, 13.2).
enum class TapeFormat
{
    UncompressedPcm, ///< no decode cost; only the Lavish tier can afford the bytes
    Flac             ///< lossless, roughly half the storage, decode cost at read
};

/// The ASRC quality setting at the membranes (white paper Part 13.2). Higher
/// quality costs more DSP per sample.
enum class AsrcQuality
{
    VeryHigh, ///< VHQ
    High,     ///< HQ
    Medium    ///< MQ
};

/// How hosted effect chains are realised under a tier (white paper Part 13.2).
enum class EffectStrategy
{
    AllLive,           ///< every effect chain runs live, every cycle
    MixedLiveCached,   ///< stable chains are rendered once and cached
    AggressiveCaching  ///< render-and-cache wherever possible to spare the CPU
};

/// The hardware measurements the startup assessment consumes (white paper Part
/// 13.1). Gathering these is platform-specific probing — the operator-run half
/// of M4; `selectTier` is the pure decision made *from* them, so it can be
/// exercised exhaustively in the headless test harness.
struct HardwareProfile
{
    int          cpuCores                 { 1 };
    bool         hasVectorUnit            { false }; ///< SSE/AVX/NEON available
    std::int64_t ramTotalBytes            { 0 };
    std::int64_t ramAvailableBytes        { 0 };
    std::int64_t storageWriteBytesPerSec  { 0 };
    int          audioBufferFrames        { 0 };     ///< device buffer size
    bool         onBattery                { false };
    bool         thermallyThrottled       { false };
};

/// The resolved policy a tier dictates — the white paper Part 13.2 table, made
/// concrete. The performer never sees this; the engine and persistence layers
/// consult it.
struct TierPolicy
{
    TapeFormat     tapeFormat;
    AsrcQuality    asrcQuality;
    EffectStrategy effectStrategy;
    int            ringDepthSeconds; ///< nominal retroactive-ring depth (Part 6.4)
};

/// The startup capability assessment (white paper Part 13.1): a pure function
/// from measured hardware to a locked tier. It starts optimistic from raw
/// compute and memory headroom, then demotes for every constraint that means
/// the hardware will not reliably deliver the higher tier — no vector unit,
/// running on battery, thermal throttling, an oversized audio buffer. The floor
/// is Survival; the system always runs.
CapabilityTier selectTier (const HardwareProfile& hardware);

/// The policy for a tier — the white paper Part 13.2 table.
TierPolicy policyFor (CapabilityTier tier);

/// A stable human-readable name, for the startup announcement to the performer
/// (white paper Part 13.4 — the system tells the performer which tier it chose).
const char* toString (CapabilityTier tier);

} // namespace ida
