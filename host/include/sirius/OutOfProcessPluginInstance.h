#pragma once

#include <cstddef>
#include <string>

namespace juce { class File; }

namespace sirius
{

/// Engine-side handle for one `sirius_plugin_host` child process (V7 §9.1,
/// plan M7). Spawns the host binary at construction, exposes byte-stream
/// IPC over the child's stdin/stdout (S1 transport — S2 swaps in POSIX
/// shared-memory + SPSC rings), and tears the child down cleanly at
/// shutdown.
///
/// **Message-thread only.** This class has no audio-thread surface: every
/// public method may block, allocate, or invoke system calls. The S2 path
/// will add a separate audio-thread-safe `pump()` over the shared-memory
/// rings; until then, do not call these from `processBlock`-equivalent
/// contexts.
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
    int  stdinWriteFd_  = -1; ///< parent's write end of child's stdin
    int  stdoutReadFd_  = -1; ///< parent's read end of child's stdout
    int  childPid_      = -1; ///< -1 once reaped
    bool shutdownCalled_ = false;

    /// Closes the stdin write fd if still open; signals EOF to the child.
    void closeStdinWrite() noexcept;

    /// Reaps the child non-blockingly. Returns true if the child is gone.
    bool reapIfExited() noexcept;
};

} // namespace sirius
