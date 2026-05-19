#pragma once

#include "sirius/PluginGuiState.h"
#include "sirius/PluginIpcMessage.h"
#include "sirius/PluginStateRegion.h"
#include "sirius/SharedMemoryRegion.h"
#include "sirius/SharedMemorySpscQueue.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace juce { class File; }

namespace sirius
{

/// Engine-side handle for one `sirius_plugin_host` child process (V7 §9.1,
/// plan M7). Spawns the host binary at construction, exposes byte-stream
/// IPC over the child's stdin/stdout (S1 transport — S2 swaps in POSIX
/// shared-memory + SPSC rings), and tears the child down cleanly at
/// shutdown.
///
/// **Mixed thread surface (M7 S3+).** Most methods are message-thread only —
/// `sendBytes`/`readBytes`, the constructors, `shutdown`, `isRunning` —
/// they may block, allocate, or invoke system calls. The audio-thread
/// surface is the two `try…` methods added in M7 S3: `tryWriteBytes` and
/// `tryReadBytes`. Both wrap the underlying `SharedMemorySpscQueue` push
/// and pop directly (already wait-free noexcept per
/// `core/include/sirius/SharedMemorySpscQueue.h:103-124`), do exactly one
/// SPSC operation, allocate nothing, and never spin.
///
/// The instance is non-copyable + non-movable. The owner keeps it alive
/// for the lifetime of the hosted plug-in slot; `OutputMixer` will own
/// one per Constituent::EffectChain slot once S3 wires it in.
///
/// Deviation from M7 S1 spec: the original orchestrator decision was to
/// implement this on `juce::ChildProcess`, but JUCE's ChildProcess only
/// exposes a *read* path over the child's stdout — it cannot write to
/// the child's stdin. Identity pass-through requires both directions, so
/// this class uses POSIX `fork`/`pipe`/`execvp` directly. macOS + Linux
/// only for now; Windows lands when the platform-completion roadmap
/// (M23+) reaches it, per the macOS→iOS→Windows→Linux platform order.
class OutOfProcessPluginInstance
{
public:
    /// Spawns the host child process in identity mode (byte-stream
    /// pass-through over stdin/stdout). `hostBinaryPath` must point at
    /// the built `sirius_plugin_host` executable; `instanceId` is
    /// forwarded via `--instance-id` (logged only in S1; will name the
    /// shared-mem segment in S2c). The constructor returns whether or
    /// not the spawn succeeded — call `isRunning()` immediately after
    /// to find out.
    OutOfProcessPluginInstance (const juce::File& hostBinaryPath,
                                std::string instanceId);

    /// Spawns the host child process in CLAP mode — the host dlopens
    /// `clapPluginBundle` and pumps interleaved-stereo float buffers
    /// through its CLAP `process()` callback. Wire format on stdin/stdout
    /// (S2a transport, swapped for shared-mem in S2c):
    ///   per buffer: uint32_t frameCount, then frameCount × 2 × float (L,R).
    /// Otherwise identical behaviour to the identity-mode constructor —
    /// same shutdown / reap / isRunning semantics.
    OutOfProcessPluginInstance (const juce::File& hostBinaryPath,
                                std::string instanceId,
                                const juce::File& clapPluginBundle);

    /// Tears down the child cleanly via `shutdown()` if not already done.
    ~OutOfProcessPluginInstance();

    OutOfProcessPluginInstance (const OutOfProcessPluginInstance&) = delete;
    OutOfProcessPluginInstance& operator= (const OutOfProcessPluginInstance&) = delete;
    OutOfProcessPluginInstance (OutOfProcessPluginInstance&&) = delete;
    OutOfProcessPluginInstance& operator= (OutOfProcessPluginInstance&&) = delete;

    /// True while the child process is alive (spawned and not yet reaped).
    /// Non-const because a positive `waitpid(WNOHANG)` reap silently collects
    /// the zombie AND updates internal state so a later `shutdown()` does
    /// not signal a recycled PID. `noexcept` so it stays safe to call from a
    /// destructor's error path.
    bool isRunning() noexcept;

    /// Test-only accessor — returns the underlying POSIX pid (>0 while
    /// alive, -1 after shutdown/reap). Lets tests verify the child actually
    /// exited (`kill(pid, 0)` returns ESRCH) rather than relying on a
    /// structural placeholder assertion.
    long childPidForTesting() const noexcept;

    /// Writes `count` bytes from `data` into the child's stdin, retrying
    /// on `EINTR` and partial writes. Returns true if all bytes were
    /// delivered; false if the write pipe is closed or the child exited.
    bool sendBytes (const std::byte* data, std::size_t count);

    /// Reads up to `capacity` bytes from the child's stdout into `buffer`,
    /// blocking for at most `timeoutMs` milliseconds total. Returns the
    /// number of bytes actually read; 0 on timeout or clean EOF. Negative
    /// timeouts block indefinitely until data or EOF.
    std::size_t readBytes (std::byte* buffer, std::size_t capacity, int timeoutMs);

    /// Audio-thread sibling of `sendBytes` (M7 S3). Packages `count`
    /// bytes into a single `PluginIpcMessage` and attempts ONE wait-free
    /// `SharedMemorySpscQueue::push`. Returns true if the push succeeded;
    /// false if the engine→host ring is full, `count` exceeds
    /// `PluginIpcMessage::kMaxPayloadBytes`, or the queue is not attached.
    ///
    /// No retries, no spin loops, no timeouts, no allocation, no syscalls.
    /// Bounded execution per the M7 S3 row in `docs/RT_SAFETY_CONTRACT.md
    /// §6`. The caller (e.g. `OutOfProcessEffectChainHost::pumpSlot`) is
    /// responsible for "miss" handling — a `false` return on a full ring
    /// is the dry-on-miss path.
    bool tryWriteBytes (const std::byte* data, std::size_t count) noexcept;

    /// Audio-thread sibling of `readBytes` (M7 S3). Attempts ONE wait-free
    /// `SharedMemorySpscQueue::pop` from the host→engine ring; on success,
    /// copies up to `capacity` bytes from the popped message's payload
    /// into `buffer` and sets `bytesRead` to the number of bytes written.
    /// Returns true if a message was popped (even if the payload was
    /// zero-length); false (with `bytesRead = 0`) if the ring is empty
    /// or the queue is not attached.
    ///
    /// Unlike `readBytes`, this method does NOT preserve leftover bytes
    /// across calls — a single message is consumed in a single call. If
    /// the popped payload is larger than `capacity`, the excess is
    /// silently discarded (the caller's contract is to pass a buffer big
    /// enough to hold one frame's worth of audio bytes).
    ///
    /// No retries, no spin loops, no timeouts, no allocation, no syscalls.
    /// Bounded execution per the M7 S3 row in `docs/RT_SAFETY_CONTRACT.md
    /// §6`.
    bool tryReadBytes (std::byte* buffer, std::size_t capacity,
                       std::size_t& bytesRead) noexcept;

    // ---- Editor (M7 S5) — message-thread only --------------------------
    //
    // Non-blocking publish-and-poll API onto the shared `PluginGuiState`
    // region. `request*` writes the new fields and bumps `requestSeq` with
    // release ordering; the host child services asynchronously and writes
    // back via `responseSeq`. Reads (`editorCaContextId`, `editorSize`)
    // acquire-load the response fields, returning 0 / {0,0} until the host
    // has serviced the latest request. The parent UI is expected to poll
    // (e.g. via a JUCE Timer at GUI cadence). Bounded to a single shared
    // region — no rings involved, naturally MPMC-safe.

    /// Publishes a Show request at `(width, height)` points. Returns false
    /// only if the GUI state region failed to map at construction time.
    bool requestEditorShow (std::uint32_t width, std::uint32_t height) noexcept;

    /// Publishes a Hide request. Returns false only on missing GUI region.
    bool requestEditorHide() noexcept;

    /// Publishes a Resize request. Caller may issue this any time after a
    /// Show. Returns false only on missing GUI region.
    bool requestEditorResize (std::uint32_t width, std::uint32_t height) noexcept;

    /// Returns the most-recent CAContextID the host published, or 0 if no
    /// editor is currently shown (or the latest Show request has not yet
    /// been serviced). Acquire-load.
    std::uint32_t editorCaContextId() const noexcept;

    /// Returns `(width, height)` the host published with the last serviced
    /// Show / Resize, or `{0, 0}` if nothing has been serviced yet.
    /// Acquire-loaded under the same protocol as `editorCaContextId`.
    std::pair<std::uint32_t, std::uint32_t> editorSize() const noexcept;

    /// Returns the engine-side request seq counter — the value the host
    /// must bump `responseSeq` to in order to mark the request serviced.
    /// Exposed so callers (notably the supervisor's restart re-publish
    /// path) can wait for completion without scraping internal state.
    std::uint64_t editorRequestSeq() const noexcept;

    /// Returns the response seq the host has bumped to. `requestSeq ==
    /// responseSeq` means the latest request has been fully serviced.
    std::uint64_t editorResponseSeq() const noexcept;

    // ---- State IPC (M8 S2) — message-thread only -----------------------
    //
    // Round-trips plug-in state bytes through the dedicated state shm
    // region. The child services on its idle / per-buffer cadence (see
    // host_process/main.cpp::serviceStateRequests).

    /// Asks the child to invoke `clap_plugin_state.save`. On success
    /// `outBytes` receives the plug-in's state; returns true. On
    /// timeout, plug-in error, or absent state region: returns false
    /// and leaves `outBytes` empty.
    bool requestStateSave (std::vector<std::byte>& outBytes,
                           std::chrono::milliseconds timeout) noexcept;

    /// Asks the child to invoke `clap_plugin_state.load`. Returns true
    /// only if the plug-in's load callback returned true. Bytes longer
    /// than `PluginStateState::kMaxStateBytes` return false immediately
    /// without IPC.
    bool requestStateLoad (std::span<const std::byte> bytes,
                           std::chrono::milliseconds timeout) noexcept;

    /// Closes the child's stdin (signals EOF), waits up to
    /// `kShutdownGraceMs` for the child to exit on its own, then sends
    /// SIGKILL if it has not. Idempotent — safe to call multiple times.
    void shutdown();

    /// The instance id this child was spawned with — useful for log lines
    /// and for tests that need to disambiguate multiple instances.
    const std::string& instanceId() const noexcept { return instanceId_; }

    /// Time the destructor / shutdown() will wait for the child to exit
    /// cleanly after closing its stdin, before escalating to SIGKILL.
    static constexpr int kShutdownGraceMs = 500;

private:
    std::string instanceId_;
    int  childPid_       = -1; ///< -1 once reaped
    bool shutdownCalled_ = false;

    /// Engine→host ring backing storage (parent owns + shm_unlinks).
    std::unique_ptr<SharedMemoryRegion> engineToHostRegion_;
    /// Host→engine ring backing storage (parent owns + shm_unlinks).
    std::unique_ptr<SharedMemoryRegion> hostToEngineRegion_;
    /// Engine→host SPSC queue placement-new'd into engineToHostRegion_.
    std::unique_ptr<SharedMemorySpscQueue<PluginIpcMessage>> engineToHostQueue_;
    /// Host→engine SPSC queue placement-new'd into hostToEngineRegion_.
    std::unique_ptr<SharedMemorySpscQueue<PluginIpcMessage>> hostToEngineQueue_;

    /// Editor control region (M7 S5). Engine owns + shm_unlinks; host
    /// child opens by name. nullptr if shm_open failed at construction.
    std::unique_ptr<SharedMemoryRegion> guiStateRegion_;
    /// Typed view into `guiStateRegion_->data()`. Owned by the region.
    /// nullptr if the region failed to map.
    PluginGuiState*                     guiState_ { nullptr };
    /// Last requestSeq the engine issued. Cached so request methods can
    /// bump monotonically without re-reading the atomic. Message-thread
    /// only (no atomic needed).
    std::uint64_t                       lastRequestSeq_ { 0 };

    /// State IPC region (M8 S2). Engine owns + shm_unlinks; host child
    /// opens by name. nullptr if shm_open failed at construction.
    std::unique_ptr<SharedMemoryRegion> stateRegion_;
    /// Typed view into `stateRegion_->data()`. nullptr if it failed to map.
    PluginStateState*                   stateState_ { nullptr };
    /// Last state requestSeq the engine issued. Message-thread only.
    std::uint64_t                       lastStateRequestSeq_ { 0 };

    /// Leftover bytes from a partially-consumed message — preserves the
    /// "stop at first non-empty read" stream semantics across calls that
    /// pull less than a whole payload.
    PluginIpcMessage leftoverMessage_ {};
    std::size_t      leftoverCursor_  { 0 };

    /// Reaps the child non-blockingly. Returns true if the child is gone.
    bool reapIfExited() noexcept;

    /// Creates the M8 S2 state shm region (CreateExclusive) and inits the
    /// PluginStateState in place. Tolerates shm_open failure by leaving
    /// `stateRegion_`/`stateState_` null — both constructors call this.
    void createStateRegion();
};

} // namespace sirius
