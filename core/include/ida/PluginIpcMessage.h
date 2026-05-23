#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace ida
{

/// Fixed-size IPC message exchanged between the engine and a hosted
/// plug-in's child process over the M7 S2c shared-memory SPSC rings.
///
/// Trivially copyable POD by construction — `SharedMemorySpscQueue<T>`
/// rejects anything else at compile time, and shared-memory communication
/// can't run destructors across the process boundary. The two ends of the
/// IPC channel each compile against this header to guarantee an identical
/// layout.
///
/// Variable-length payloads are simulated via fixed `payload[kMaxPayloadBytes]`
/// with `payloadBytes` recording how many bytes are actually valid — the
/// same pattern M6's NotificationBus uses for its category rings. The
/// ceiling is sized to fit one stereo audio buffer at the 1024-frame
/// envelope (2 × 1024 × sizeof(float) = 8192) with headroom; parameter
/// events are well under.
///
/// `monotonicNs` is the LMC sample index at producer-push time as of S3+
/// (was `steady_clock` ns through S2). Reinterpreted, not retyped; the
/// field stays `int64_t` so consumer code that wants wall-clock latency
/// reads it as before. S3 producer-side (`OutOfProcessPluginInstance::
/// tryWriteBytes`) currently writes `0` here — the LMC handle is not yet
/// surfaced to the audio-thread caller (`OutOfProcessEffectChainHost::
/// pumpSlot`). M7 S4+ watchdog work fills the real LMC sample index in.
/// The existing S2 `sendBytes` / host-side `RingByteStream::flush` paths
/// still write the steady_clock timestamp, so the `[plugin-ipc][.rt-smoke]`
/// latency case keeps working unchanged.
struct PluginIpcMessage
{
    enum Kind : std::uint32_t
    {
        /// Raw byte-stream chunk — preserves the byte-oriented public
        /// API of `OutOfProcessPluginInstance::sendBytes` / `readBytes`.
        Bytes      = 0,

        /// Reserved for M7 S3+ — parameter-change events with sample-
        /// accurate scheduling.
        Parameter  = 1
    };

    /// Maximum payload bytes per message. One stereo audio buffer at the
    /// 1024-frame engine envelope = 8192 bytes; rounded to a power of two
    /// for cache-line friendliness.
    static constexpr std::size_t kMaxPayloadBytes = 8192;

    /// Producer-side monotonic timestamp (steady_clock nanoseconds).
    /// Promoted to LMC-domain when S3 wires the audio thread.
    std::int64_t  monotonicNs   { 0 };

    /// One of `Kind`. `uint32_t` so the layout is unambiguous across
    /// translation units that include this header.
    std::uint32_t kind          { Bytes };

    /// Bytes valid in `payload` (≤ kMaxPayloadBytes).
    std::uint32_t payloadBytes  { 0 };

    /// Variable-content payload.
    std::byte     payload[kMaxPayloadBytes] {};
};

static_assert (std::is_trivially_copyable_v<PluginIpcMessage>,
               "PluginIpcMessage must be POD — lives in shared memory");
static_assert (std::is_trivially_destructible_v<PluginIpcMessage>,
               "PluginIpcMessage destructors do not run across process boundaries");

/// Per-instance ring capacity. 64 slots matches M6 NotificationBus
/// precedent for a "rarely backs up" surface; revisited if the M7 S4
/// watchdog work surfaces overflow.
constexpr std::size_t kPluginIpcRingCapacity = 64;

/// Builds the shm name used for the engine→host ring of a given instance.
/// macOS shm_open names are capped at 31 chars including the leading
/// slash; the prefix `/ida.` (8 chars) + suffix `.e2h` (4 chars) leaves
/// 19 chars for the instance id. Long ids must be hashed by the caller.
inline std::string makeEngineToHostRingName (const std::string& instanceId)
{
    return "/ida." + instanceId + ".e2h";
}

inline std::string makeHostToEngineRingName (const std::string& instanceId)
{
    return "/ida." + instanceId + ".h2e";
}

} // namespace ida
