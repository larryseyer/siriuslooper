#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace sirius
{

/// Shared-memory state for plug-in `clap_plugin_state` save/load IPC
/// (M8 S2). One region per `OutOfProcessPluginInstance`, mapped by
/// both engine and host child. Modelled exactly on `PluginGuiState` —
/// same request/response sequence-counter pattern — so the host
/// child's idle-tick service loop can use one body for both regions.
///
/// **Why a fourth shm region rather than reusing the audio rings?**
/// The audio rings carry CLAP-mode wire bytes (`uint32_t frameCount`
/// + interleaved stereo floats). Interleaving a control payload
/// onto them would require a tagged-message protocol and complicate
/// the RT-path code. State traffic is rare (save/load at operator
/// boundaries, not per-buffer) and arbitrary in size; a dedicated
/// region keeps the audio path clean.
///
/// **Why 64 KiB caps?** Covers EQs, compressors, and most synths.
/// Wavetable / sampler plug-ins that serialize embedded samples
/// exceed this. The out-of-bounds case: the child sees
/// `outBytes.size() > kMaxStateBytes` after the plug-in's
/// `clap_plugin_state.save` callback returns and writes
/// `responseStatus = 1`. The engine's `requestStateSave` returns
/// false; the populator falls back to descriptor-only and posts a
/// Warning notification. Chunked / streaming state IPC is the
/// follow-up when a real plug-in needs it; out of scope for M8 S2.
///
/// **Restart semantics.** When the supervisor restarts a child, the
/// engine destroys the old region and creates a fresh one — both
/// `requestSeq` and `responseSeq` reset to 0. No replay of in-flight
/// state requests; the operator must re-trigger save/load. (State
/// save is operator-initiated; supervisor restart in the middle of a
/// save is essentially never observed.)
struct PluginStateState
{
    /// What the engine is asking the host to do this request seq.
    enum RequestKind : std::uint32_t
    {
        None = 0,
        Save = 1,
        Load = 2
    };

    /// Maximum bytes per direction. 64 KiB — see class docblock.
    static constexpr std::uint32_t kMaxStateBytes = 64u * 1024u;

    /// Response status codes.
    enum Status : std::uint32_t
    {
        Ok                 = 0,
        ErrorGeneric       = 1,
        ErrorTooLarge      = 2,
        ErrorNotSupported  = 3
    };

    // --- Engine writes; host reads. --------------------------------------

    std::atomic<std::uint64_t> requestSeq    { 0 };
    std::atomic<std::uint32_t> requestKind   { None };
    std::atomic<std::uint32_t> requestBytes  { 0 };
    char                       requestPayload [kMaxStateBytes] {};

    // --- Host writes; engine reads. --------------------------------------

    std::atomic<std::uint64_t> responseSeq    { 0 };
    std::atomic<std::uint32_t> responseStatus { Ok };
    std::atomic<std::uint32_t> responseBytes  { 0 };
    char                       responsePayload [kMaxStateBytes] {};

    /// Placement-construct a zero-initialized `PluginStateState` at
    /// `mem`. Use on the creator side after `SharedMemoryRegion`
    /// creation. The region's bytes are already zero (POSIX
    /// `ftruncate` semantics), so this writes the same zeros and
    /// returns a typed pointer.
    static PluginStateState* initInPlace (void* mem) noexcept
    {
        return ::new (mem) PluginStateState();
    }

    /// Reinterpret an existing zero-initialized region as a
    /// `PluginStateState`. Used by the host child after `OpenExisting`.
    static PluginStateState* view (void* mem) noexcept
    {
        return reinterpret_cast<PluginStateState*> (mem);
    }
};

/// Builds the shm name used for the per-instance state region.
/// Mirrors `makeEngineToHostRingName` / `makeGuiStateRegionName` —
/// `/sirius.<instanceId>.state`.
inline std::string makeStateRegionName (const std::string& instanceId)
{
    return "/sirius." + instanceId + ".state";
}

} // namespace sirius
