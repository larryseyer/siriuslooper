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
/// exceed this. Over-cap is surfaced differently per direction:
///
/// - **Save-side over-cap → `Status::ErrorGeneric`.** The host child
///   hands `clap_plugin_state.save` an output stream backed by the
///   fixed `responsePayload` buffer. When the plug-in writes past
///   `kMaxStateBytes`, that ostream write fails inside the plug-in's
///   own save callback, the callback returns failure, and the child
///   writes `responseStatus = ErrorGeneric`. The size is not known up
///   front (the plug-in streams incrementally), so it cannot be
///   rejected as `ErrorTooLarge`.
/// - **Load-side over-cap → `Status::ErrorTooLarge`.** A Load request
///   carries its byte count in `requestBytes`, so the child rejects it
///   up front when `requestBytes > kMaxStateBytes`, before touching the
///   plug-in, and writes `responseStatus = ErrorTooLarge`.
/// - **Missing state extension → `Status::ErrorNotSupported`.** If the
///   plug-in exposes no `clap_plugin_state`, the child cannot service
///   either kind and writes `responseStatus = ErrorNotSupported`.
///
/// In every failure case the engine's `requestStateSave` /
/// `requestStateLoad` returns false; on Save the populator falls back
/// to descriptor-only and posts a Warning notification. Chunked /
/// streaming state IPC is the follow-up when a real plug-in needs it;
/// out of scope for M8 S2. (`serviceStateRequests` lives on the host
/// child — Task 6.)
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

    /// Monotonically increasing per-instance request counter. Engine bumps
    /// it AFTER writing `requestKind` / `requestBytes` / `requestPayload`,
    /// with release ordering, so the host's acquire-load of `requestSeq`
    /// sees the new fields. This seq/release pairing is the entire
    /// synchronization contract for the engine→host direction.
    std::atomic<std::uint64_t> requestSeq    { 0 };

    /// One of `RequestKind`. Set by engine before bumping `requestSeq`.
    std::atomic<std::uint32_t> requestKind   { None };

    /// Payload length in bytes. On `Save` it is 0 (host fills the
    /// response); on `Load` it is the length written into `requestPayload`,
    /// which the host validates against `kMaxStateBytes` before use.
    std::atomic<std::uint32_t> requestBytes  { 0 };

    /// State bytes for a `Load` request. Unused on `Save`.
    char                       requestPayload [kMaxStateBytes] {};

    // --- Host writes; engine reads. --------------------------------------

    /// Per-request response counter. Host bumps it to match `requestSeq`
    /// AFTER writing `responseStatus` / `responseBytes` / `responsePayload`,
    /// with release ordering, so the engine's acquire-load of `responseSeq`
    /// sees the new fields. Symmetric to the engine-side contract above.
    std::atomic<std::uint64_t> responseSeq    { 0 };

    /// One of `Status`. Set by host before bumping `responseSeq`.
    std::atomic<std::uint32_t> responseStatus { Ok };

    /// Length written into `responsePayload` on a successful `Save`.
    std::atomic<std::uint32_t> responseBytes  { 0 };

    /// State bytes produced by a `Save` request. Unused on `Load`.
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

static_assert (sizeof (PluginStateState) <= 256u * 1024u,
               "PluginStateState must fit the per-instance shm budget — "
               "two 64KiB payload buffers plus a handful of atomics");

/// Builds the shm name used for the per-instance state region.
/// Mirrors `makeEngineToHostRingName` / `makeGuiStateRegionName` —
/// `/sirius.<instanceId>.state`.
inline std::string makeStateRegionName (const std::string& instanceId)
{
    return "/sirius." + instanceId + ".state";
}

} // namespace sirius
