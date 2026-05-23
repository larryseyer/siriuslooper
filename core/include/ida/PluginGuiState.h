#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace ida
{

/// Shared-memory state for plug-in editor control (M7 S5 — macOS GUI
/// embedding). One region per `OutOfProcessPluginInstance`, mapped by both
/// engine and host child. The engine UI publishes editor lifecycle requests
/// (show / hide / resize) by writing the request fields and bumping
/// `requestSeq`; the host child polls `requestSeq`, services the request via
/// the plug-in's `clap_gui` extension, and writes the result fields plus
/// `responseSeq`.
///
/// **Why not a third SPSC ring pair?** The M7 audio rings already conflate
/// audio-thread (`tryWriteBytes`) and message-thread (`sendBytes`) producers
/// — a latent SPSC violation that's harmless today because the two paths
/// don't race in the existing tests but would become a real bug as soon as
/// the operator could open a plug-in editor mid-playback. Rather than
/// compound that by adding a fourth+fifth queue, S5 uses a single
/// fixed-size POD region with seq-based atomic synchronization: every
/// field is `std::atomic`, naturally MPMC-safe, no ring discipline needed.
/// Editor traffic is rare and small (a few events per operator session),
/// so the polling model has no realistic cost.
///
/// **Restart semantics.** When the supervisor restarts a child, the engine
/// destroys the old region and creates a fresh one — both `requestSeq` and
/// `responseSeq` reset to 0. The supervisor is responsible for re-issuing
/// the last known-good request (see `OutOfProcessEffectChainHost::
/// attemptRestart`); the parent UI's polling loop will pick up the new
/// `responseContextId` once the new child services the re-issued request.
///
/// **Wire safety.** All fields are `std::atomic` of standard integer types
/// for which the standard guarantees standard-layout + lock-free behaviour
/// (`std::atomic_is_lock_free` of `uint32_t` / `uint64_t` returns true on
/// every platform IDA supports). Zero-initialization (which is what
/// `SharedMemoryRegion::CreateExclusive` gives us after `ftruncate`)
/// matches the in-class initializers, so no explicit placement-new is
/// strictly required; the `initInPlace` helper exists to make the
/// construction site self-documenting.
struct PluginGuiState
{
    /// What the engine is asking the host to do this request seq.
    enum RequestKind : std::uint32_t
    {
        /// No request outstanding (initial state; also written back to
        /// `requestKind` after a hide is fully serviced — but mostly the
        /// kind is preserved so the host can re-service after a restart).
        None   = 0,
        /// Create (or re-create) the editor view at the requested size.
        Show   = 1,
        /// Tear the editor view down.
        Hide   = 2,
        /// Resize the existing editor view to the new dimensions.
        Resize = 3
    };

    // --- Engine writes; host reads. --------------------------------------

    /// Monotonically increasing per-instance request counter. Engine bumps
    /// it AFTER writing `requestKind` / `requestWidth` / `requestHeight`.
    /// Released so the host's acquire-load sees the new fields.
    std::atomic<std::uint64_t> requestSeq    { 0 };

    /// One of `RequestKind`. Set by engine before bumping `requestSeq`.
    std::atomic<std::uint32_t> requestKind   { None };

    /// Requested editor width in points. Used by `Show` and `Resize`.
    std::atomic<std::uint32_t> requestWidth  { 0 };

    /// Requested editor height in points. Used by `Show` and `Resize`.
    std::atomic<std::uint32_t> requestHeight { 0 };

    // --- Host writes; engine reads. --------------------------------------

    /// Per-request response counter. Host bumps it to match `requestSeq`
    /// AFTER writing the response fields. Released so the engine's
    /// acquire-load sees the new fields.
    std::atomic<std::uint64_t> responseSeq       { 0 };

    /// `CAContext.contextId` for the embedded editor's CALayer tree, or 0
    /// if the editor is not currently shown (or the plug-in does not
    /// support `clap_gui_cocoa`).
    std::atomic<std::uint32_t> responseContextId { 0 };

    /// Actual editor width the plug-in chose. May differ from
    /// `requestWidth` (plug-ins are allowed to size-constrain).
    std::atomic<std::uint32_t> responseWidth     { 0 };

    /// Actual editor height the plug-in chose.
    std::atomic<std::uint32_t> responseHeight    { 0 };

    /// Placement-construct a zero-initialized `PluginGuiState` at `mem`.
    /// Use on the creator side after `SharedMemoryRegion` creation; the
    /// region's bytes are already zero (POSIX `ftruncate` semantics), so
    /// this just writes the same zeros again and returns a typed pointer.
    /// The consumer side calls `view(mem)` instead — same layout, no
    /// construction.
    static PluginGuiState* initInPlace (void* mem) noexcept
    {
        return ::new (mem) PluginGuiState();
    }

    /// Reinterpret an existing zero-initialized region as a
    /// `PluginGuiState`. Used by the host child after `OpenExisting`.
    static PluginGuiState* view (void* mem) noexcept
    {
        return reinterpret_cast<PluginGuiState*> (mem);
    }
};

static_assert (sizeof (PluginGuiState) <= 256,
               "PluginGuiState must stay tiny — one cache line of atomics is plenty");

/// Builds the shm name used for the per-instance GUI state region. Tracks
/// `makeEngineToHostRingName` from `PluginIpcMessage.h`; the `.gui` suffix
/// (4 chars including the dot) leaves room for the same 18-char instance
/// id budget the audio rings use.
inline std::string makeGuiStateRegionName (const std::string& instanceId)
{
    return "/ida." + instanceId + ".gui";
}

} // namespace ida
