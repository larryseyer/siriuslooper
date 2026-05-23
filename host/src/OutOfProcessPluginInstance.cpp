#include "ida/OutOfProcessPluginInstance.h"

#include "ida/PluginGuiState.h"
#include "ida/PluginIpcMessage.h"
#include "ida/PluginStateRegion.h"
#include "ida/SharedMemoryRegion.h"
#include "ida/SharedMemorySpscQueue.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace sirius
{

namespace
{
    /// Poll interval used while waiting for the child to exit during
    /// shutdown(). Short enough that a clean exit is observed promptly,
    /// long enough that we don't burn the CPU spinning on waitpid().
    constexpr int kShutdownPollMs = 5;

    /// Audio-thread-friendly backoff for ring polling. The engine side is
    /// driven from the message thread for now (S3 will move audio-thread
    /// traffic onto AudioCallback), so a brief spin + short sleep is
    /// fine. Total per-iteration cost dominates over the latency contract
    /// only when the host is genuinely starved.
    constexpr int kRingSpinIterations    = 32;
    constexpr int kRingPollMicroseconds  = 50;

    /// Max wait time per chunk push when the engine→host ring is full.
    /// 100 ms is generous — covers a transient stall but surfaces a real
    /// deadlock as a `sendBytes` failure rather than a hang.
    constexpr int kRingPushTimeoutMs = 100;

    /// Poll-sleep interval while the message thread waits on a state
    /// save/load response from the child. Shared by requestStateSave and
    /// requestStateLoad.
    constexpr auto kStatePollInterval = std::chrono::microseconds (100);

    /// Spawn the host child. `instanceId` is forwarded via `--instance-id`
    /// (also used by the child as the shm segment name root). On success
    /// `outPid` holds the live pid; on failure it stays -1.
    void spawnHostChild (const std::string& binaryPath,
                         const std::string& instanceId,
                         const std::string& mode,
                         const std::string& pluginPath,
                         int& outPid)
    {
        outPid = -1;

        const pid_t pid = ::fork();
        if (pid < 0)
            return;

        if (pid == 0)
        {
            // Child: exec the host binary. No stdin/stdout rewiring — the
            // child opens the shm rings by name via `--instance-id`.
            std::vector<std::string> argvStorage;
            argvStorage.reserve (7);
            argvStorage.push_back (binaryPath);
            argvStorage.emplace_back ("--instance-id");
            argvStorage.push_back (instanceId);
            argvStorage.emplace_back ("--mode");
            argvStorage.push_back (mode);
            if (! pluginPath.empty())
            {
                argvStorage.emplace_back ("--plugin-path");
                argvStorage.push_back (pluginPath);
            }

            std::vector<char*> argv;
            argv.reserve (argvStorage.size() + 1);
            for (auto& s : argvStorage)
                argv.push_back (s.data());
            argv.push_back (nullptr);

            ::execvp (binaryPath.c_str(), argv.data());
            ::_exit (127); // exec failed
        }

        outPid = static_cast<int> (pid);
    }
}

OutOfProcessPluginInstance::OutOfProcessPluginInstance (const juce::File& hostBinaryPath,
                                                        std::string instanceId)
    : instanceId_ (std::move (instanceId))
{
    try
    {
        const auto e2hName = makeEngineToHostRingName (instanceId_);
        const auto h2eName = makeHostToEngineRingName (instanceId_);
        const auto bytes   = SharedMemorySpscQueue<PluginIpcMessage>::bytesNeeded (
                                 kPluginIpcRingCapacity);

        engineToHostRegion_ = std::make_unique<SharedMemoryRegion> (
            e2hName, bytes, SharedMemoryRegion::Mode::CreateExclusive);
        hostToEngineRegion_ = std::make_unique<SharedMemoryRegion> (
            h2eName, bytes, SharedMemoryRegion::Mode::CreateExclusive);

        engineToHostQueue_ = std::make_unique<SharedMemorySpscQueue<PluginIpcMessage>> (
            SharedMemorySpscQueue<PluginIpcMessage>::create (
                engineToHostRegion_->data(), kPluginIpcRingCapacity));
        hostToEngineQueue_ = std::make_unique<SharedMemorySpscQueue<PluginIpcMessage>> (
            SharedMemorySpscQueue<PluginIpcMessage>::create (
                hostToEngineRegion_->data(), kPluginIpcRingCapacity));

        // M7 S5 — per-instance GUI state region. Engine creates + zero-
        // initializes; host child OpenExistings on the same name. Region
        // size is one page; PluginGuiState fits in a single cache line
        // and the page rounding (16 KiB on Apple Silicon) gives us
        // ample headroom for future expansion.
        guiStateRegion_ = std::make_unique<SharedMemoryRegion> (
            makeGuiStateRegionName (instanceId_), sizeof (PluginGuiState),
            SharedMemoryRegion::Mode::CreateExclusive);
        guiState_ = PluginGuiState::initInPlace (guiStateRegion_->data());
    }
    catch (const std::exception&)
    {
        // shm_open / mmap failure leaves childPid_ == -1; isRunning()
        // reports false and the caller's REQUIRE catches it. The regions
        // that DID construct successfully unlink themselves via RAII.
        engineToHostRegion_.reset();
        hostToEngineRegion_.reset();
        engineToHostQueue_.reset();
        hostToEngineQueue_.reset();
        guiStateRegion_.reset();
        guiState_ = nullptr;
        return;
    }

    // M8 S2 — per-instance state region for clap_plugin_state save/load
    // IPC. Independent of the rings/GUI region: a failure here leaves the
    // instance otherwise functional, so it has its own try/catch.
    createStateRegion();

    spawnHostChild (hostBinaryPath.getFullPathName().toStdString(),
                    instanceId_, "identity", {}, childPid_);
}

OutOfProcessPluginInstance::OutOfProcessPluginInstance (const juce::File& hostBinaryPath,
                                                        std::string instanceId,
                                                        const juce::File& clapPluginBundle)
    : instanceId_ (std::move (instanceId))
{
    try
    {
        const auto e2hName = makeEngineToHostRingName (instanceId_);
        const auto h2eName = makeHostToEngineRingName (instanceId_);
        const auto bytes   = SharedMemorySpscQueue<PluginIpcMessage>::bytesNeeded (
                                 kPluginIpcRingCapacity);

        engineToHostRegion_ = std::make_unique<SharedMemoryRegion> (
            e2hName, bytes, SharedMemoryRegion::Mode::CreateExclusive);
        hostToEngineRegion_ = std::make_unique<SharedMemoryRegion> (
            h2eName, bytes, SharedMemoryRegion::Mode::CreateExclusive);

        engineToHostQueue_ = std::make_unique<SharedMemorySpscQueue<PluginIpcMessage>> (
            SharedMemorySpscQueue<PluginIpcMessage>::create (
                engineToHostRegion_->data(), kPluginIpcRingCapacity));
        hostToEngineQueue_ = std::make_unique<SharedMemorySpscQueue<PluginIpcMessage>> (
            SharedMemorySpscQueue<PluginIpcMessage>::create (
                hostToEngineRegion_->data(), kPluginIpcRingCapacity));

        guiStateRegion_ = std::make_unique<SharedMemoryRegion> (
            makeGuiStateRegionName (instanceId_), sizeof (PluginGuiState),
            SharedMemoryRegion::Mode::CreateExclusive);
        guiState_ = PluginGuiState::initInPlace (guiStateRegion_->data());
    }
    catch (const std::exception&)
    {
        engineToHostRegion_.reset();
        hostToEngineRegion_.reset();
        engineToHostQueue_.reset();
        hostToEngineQueue_.reset();
        guiStateRegion_.reset();
        guiState_ = nullptr;
        return;
    }

    // M8 S2 — per-instance state region (see identity-mode ctor above).
    createStateRegion();

    spawnHostChild (hostBinaryPath.getFullPathName().toStdString(),
                    instanceId_, "clap",
                    clapPluginBundle.getFullPathName().toStdString(),
                    childPid_);
}

void OutOfProcessPluginInstance::createStateRegion()
{
    try
    {
        stateRegion_ = std::make_unique<SharedMemoryRegion> (
            makeStateRegionName (instanceId_), sizeof (PluginStateState),
            SharedMemoryRegion::Mode::CreateExclusive);
        stateState_ = PluginStateState::initInPlace (stateRegion_->data());
    }
    catch (const std::exception&)
    {
        stateRegion_.reset();
        stateState_ = nullptr;
    }
}

OutOfProcessPluginInstance::~OutOfProcessPluginInstance()
{
    if (! shutdownCalled_)
        shutdown();
}

long OutOfProcessPluginInstance::childPidForTesting() const noexcept
{
    return static_cast<long> (childPid_);
}

bool OutOfProcessPluginInstance::isRunning() noexcept
{
    if (childPid_ < 0)
        return false;

    int status = 0;
    const pid_t r = ::waitpid (childPid_, &status, WNOHANG);
    if (r == 0)
        return true; // still running
    // r > 0 (reaped zombie) OR r < 0 (already-reaped / never-existed) —
    // either way, the PID is no longer ours. Clear it so a later shutdown()
    // does NOT send a signal to a recycled PID. This is the const-correctness
    // reason isRunning() can't be const.
    childPid_ = -1;
    return false;
}

bool OutOfProcessPluginInstance::sendBytes (const std::byte* data, std::size_t count)
{
    if (engineToHostQueue_ == nullptr || data == nullptr)
        return false;

    using clock = std::chrono::steady_clock;

    std::size_t offset = 0;
    while (offset < count)
    {
        const std::size_t chunk = std::min (count - offset,
                                            PluginIpcMessage::kMaxPayloadBytes);
        PluginIpcMessage msg {};
        msg.monotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds> (
                              clock::now().time_since_epoch()).count();
        msg.kind         = PluginIpcMessage::Bytes;
        msg.payloadBytes = static_cast<std::uint32_t> (chunk);
        std::memcpy (msg.payload, data + offset, chunk);

        const auto deadline = clock::now() + std::chrono::milliseconds (kRingPushTimeoutMs);
        while (! engineToHostQueue_->push (msg))
        {
            if (clock::now() >= deadline)
                return false;
            ::usleep (kRingPollMicroseconds);
        }
        offset += chunk;
    }
    return true;
}

bool OutOfProcessPluginInstance::tryWriteBytes (const std::byte* data,
                                                std::size_t count) noexcept
{
    // Audio-thread variant: one SPSC push, no retries, no timeouts. The
    // caller's contract is that `count` fits in a single PluginIpcMessage —
    // typical CLAP-mode payload is `4 (frameCount) + N×kChannels×4` bytes,
    // so the absolute ceiling at kMaxPayloadBytes = 8192 is frameCount ≤
    // 1023 for stereo (the 1024-frame envelope is one frame over because
    // of the 4-byte header). Realistic block sizes are 64..512 and fit
    // comfortably. Caller violation on oversize → false return, no crash.
    if (engineToHostQueue_ == nullptr || data == nullptr)
        return false;
    if (count > PluginIpcMessage::kMaxPayloadBytes)
        return false;

    PluginIpcMessage msg {};
    // monotonicNs is the LMC-domain reinterpret slot per M7 decision #9.
    // S3 does not yet write an LMC sample index here — the producer-side
    // caller (`OutOfProcessEffectChainHost::pumpSlot`) sets it once the
    // engine surfaces an LMC handle to the host. S2c-era latency tests
    // that read the field as steady_clock ns are unaffected because
    // those tests use sendBytes, not tryWriteBytes.
    msg.monotonicNs  = 0;
    msg.kind         = PluginIpcMessage::Bytes;
    msg.payloadBytes = static_cast<std::uint32_t> (count);
    if (count > 0)
        std::memcpy (msg.payload, data, count);

    return engineToHostQueue_->push (msg);
}

bool OutOfProcessPluginInstance::tryReadBytes (std::byte* buffer,
                                               std::size_t capacity,
                                               std::size_t& bytesRead) noexcept
{
    // Audio-thread variant: one SPSC pop, no retries, no leftover-byte
    // stashing. Each call consumes at most one message; if the popped
    // payload is larger than `capacity`, the excess is silently dropped
    // (the caller's contract is to pass a buffer big enough to hold one
    // frame's worth of audio bytes — i.e. the same packaging the producer
    // used). The pop itself is wait-free noexcept.
    bytesRead = 0;
    if (hostToEngineQueue_ == nullptr || buffer == nullptr)
        return false;

    PluginIpcMessage msg {};
    if (! hostToEngineQueue_->pop (msg))
        return false;

    if (msg.payloadBytes == 0)
        return true; // popped, but nothing to copy

    const std::size_t take = std::min<std::size_t> (capacity, msg.payloadBytes);
    std::memcpy (buffer, msg.payload, take);
    bytesRead = take;
    return true;
}

std::size_t OutOfProcessPluginInstance::readBytes (std::byte* buffer,
                                                   std::size_t capacity,
                                                   int timeoutMs)
{
    if (hostToEngineQueue_ == nullptr || buffer == nullptr || capacity == 0)
        return 0;

    // Path 1: drain any leftover bytes from the previously-popped message
    // first. Preserves the "stop after first non-empty read" semantic the
    // S1 readExact helper depends on.
    if (leftoverCursor_ < leftoverMessage_.payloadBytes)
    {
        const std::size_t remaining = leftoverMessage_.payloadBytes - leftoverCursor_;
        const std::size_t take      = std::min (capacity, remaining);
        std::memcpy (buffer, leftoverMessage_.payload + leftoverCursor_, take);
        leftoverCursor_ += take;
        return take;
    }

    // Path 2: pop a fresh message. Spin briefly, then nanosleep/poll until
    // either the timeout elapses or a message arrives.
    using clock = std::chrono::steady_clock;
    const auto deadline = (timeoutMs < 0)
                              ? clock::time_point::max()
                              : clock::now() + std::chrono::milliseconds (timeoutMs);

    PluginIpcMessage msg {};
    while (true)
    {
        for (int i = 0; i < kRingSpinIterations; ++i)
        {
            if (hostToEngineQueue_->pop (msg))
                goto popped;
        }
        if (timeoutMs >= 0 && clock::now() >= deadline)
            return 0;
        ::usleep (kRingPollMicroseconds);
    }
popped:
    if (msg.payloadBytes == 0)
        return 0;

    const std::size_t take = std::min<std::size_t> (capacity, msg.payloadBytes);
    std::memcpy (buffer, msg.payload, take);

    if (take < msg.payloadBytes)
    {
        // Save the remainder for the next readBytes call.
        leftoverMessage_ = msg;
        leftoverCursor_  = take;
    }
    else
    {
        leftoverMessage_ = {};
        leftoverCursor_  = 0;
    }
    return take;
}

bool OutOfProcessPluginInstance::reapIfExited() noexcept
{
    if (childPid_ < 0)
        return true;

    int status = 0;
    const pid_t r = ::waitpid (childPid_, &status, WNOHANG);
    if (r == childPid_)
    {
        childPid_ = -1;
        return true;
    }
    return false;
}

void OutOfProcessPluginInstance::shutdown()
{
    if (shutdownCalled_)
        return;
    shutdownCalled_ = true;

    if (childPid_ < 0)
    {
        engineToHostQueue_.reset();
        hostToEngineQueue_.reset();
        engineToHostRegion_.reset();
        hostToEngineRegion_.reset();
        return;
    }

    // SIGTERM signals the host's pump to drop out at its next backoff
    // wake-up — same role stdin EOF used to play in the S1 pipe transport.
    ::kill (childPid_, SIGTERM);

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds (kShutdownGraceMs);
    while (clock::now() < deadline)
    {
        if (reapIfExited())
            break;
        ::usleep (kShutdownPollMs * 1000);
    }

    if (childPid_ >= 0)
    {
        ::kill (childPid_, SIGKILL);
        int status = 0;
        ::waitpid (childPid_, &status, 0);
        childPid_ = -1;
    }

    // Tear down the SPSC handles and shm regions only AFTER the child has
    // exited — otherwise an in-flight host-side pop into munmapped memory
    // would segfault and turn a clean shutdown into a SIGSEGV exit code.
    engineToHostQueue_.reset();
    hostToEngineQueue_.reset();
    engineToHostRegion_.reset();
    hostToEngineRegion_.reset();
    guiState_ = nullptr;
    guiStateRegion_.reset();
    stateState_ = nullptr;
    stateRegion_.reset();
}

// ---- Editor (M7 S5) -------------------------------------------------------
//
// Publish-and-poll API over PluginGuiState. Each request writes the new
// fields with relaxed ordering (the host child won't observe them until
// after the seq bump anyway), then bumps `requestSeq` with release. The
// child's polling loop in host_process/main.cpp acquire-loads requestSeq
// and dispatches.

bool OutOfProcessPluginInstance::requestEditorShow (std::uint32_t width,
                                                    std::uint32_t height) noexcept
{
    if (guiState_ == nullptr)
        return false;
    guiState_->requestKind  .store (PluginGuiState::Show,   std::memory_order_relaxed);
    guiState_->requestWidth .store (width,                  std::memory_order_relaxed);
    guiState_->requestHeight.store (height,                 std::memory_order_relaxed);
    guiState_->requestSeq   .store (++lastRequestSeq_,      std::memory_order_release);
    return true;
}

bool OutOfProcessPluginInstance::requestEditorHide() noexcept
{
    if (guiState_ == nullptr)
        return false;
    guiState_->requestKind  .store (PluginGuiState::Hide,   std::memory_order_relaxed);
    guiState_->requestSeq   .store (++lastRequestSeq_,      std::memory_order_release);
    return true;
}

bool OutOfProcessPluginInstance::requestEditorResize (std::uint32_t width,
                                                      std::uint32_t height) noexcept
{
    if (guiState_ == nullptr)
        return false;
    guiState_->requestKind  .store (PluginGuiState::Resize, std::memory_order_relaxed);
    guiState_->requestWidth .store (width,                  std::memory_order_relaxed);
    guiState_->requestHeight.store (height,                 std::memory_order_relaxed);
    guiState_->requestSeq   .store (++lastRequestSeq_,      std::memory_order_release);
    return true;
}

std::uint32_t OutOfProcessPluginInstance::editorCaContextId() const noexcept
{
    if (guiState_ == nullptr)
        return 0;
    // Acquire-load establishes happens-before with the host's release
    // store of responseSeq — guarantees the contextId we read is the one
    // the host wrote alongside that seq bump.
    const auto seq = guiState_->responseSeq.load (std::memory_order_acquire);
    (void) seq;
    return guiState_->responseContextId.load (std::memory_order_relaxed);
}

std::pair<std::uint32_t, std::uint32_t>
OutOfProcessPluginInstance::editorSize() const noexcept
{
    if (guiState_ == nullptr)
        return { 0, 0 };
    const auto seq = guiState_->responseSeq.load (std::memory_order_acquire);
    (void) seq;
    return { guiState_->responseWidth .load (std::memory_order_relaxed),
             guiState_->responseHeight.load (std::memory_order_relaxed) };
}

std::uint64_t OutOfProcessPluginInstance::editorRequestSeq() const noexcept
{
    if (guiState_ == nullptr) return 0;
    return guiState_->requestSeq.load (std::memory_order_acquire);
}

std::uint64_t OutOfProcessPluginInstance::editorResponseSeq() const noexcept
{
    if (guiState_ == nullptr) return 0;
    return guiState_->responseSeq.load (std::memory_order_acquire);
}

// ---- State IPC (M8 S2) ----------------------------------------------------
//
// Same publish-and-poll contract as the editor surface, but synchronous:
// the caller is the message thread at an operator-initiated save/load
// boundary, so we bump requestSeq (release) then spin-with-sleep on
// responseSeq (acquire) until the child matches the seq or the deadline
// passes. A SIGSTOPed / wedged child never bumps responseSeq, so the
// deadline check is what bounds the wait.

bool OutOfProcessPluginInstance::requestStateSave (
    std::vector<std::byte>& outBytes,
    std::chrono::milliseconds timeout) noexcept
{
    outBytes.clear();
    if (stateState_ == nullptr) return false;

    const auto seq = ++lastStateRequestSeq_;
    stateState_->requestKind .store (PluginStateState::Save,
                                     std::memory_order_relaxed);
    stateState_->requestBytes.store (0, std::memory_order_relaxed);
    stateState_->requestSeq  .store (seq, std::memory_order_release);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (stateState_->responseSeq.load (std::memory_order_acquire) >= seq)
            break;
        std::this_thread::sleep_for (kStatePollInterval);
    }

    if (stateState_->responseSeq.load (std::memory_order_acquire) < seq)
        return false; // timeout

    if (stateState_->responseStatus.load (std::memory_order_relaxed)
        != PluginStateState::Ok)
        return false;

    const auto n = stateState_->responseBytes.load (std::memory_order_relaxed);
    // Cross-process trust boundary: the child reports its own payload size,
    // but responsePayload is exactly kMaxStateBytes — anything larger is a
    // protocol violation. Clamping here also keeps `noexcept` honest by
    // bounding the resize allocation (and the memcpy read) to 64 KiB, so a
    // buggy/malicious child can't trigger an over-read or std::bad_alloc.
    if (n > PluginStateState::kMaxStateBytes)
        return false;
    outBytes.resize (n);
    std::memcpy (outBytes.data(), stateState_->responsePayload, n);
    return true;
}

bool OutOfProcessPluginInstance::requestStateLoad (
    std::span<const std::byte> bytes,
    std::chrono::milliseconds timeout) noexcept
{
    if (stateState_ == nullptr) return false;
    if (bytes.size() > PluginStateState::kMaxStateBytes) return false;

    std::memcpy (stateState_->requestPayload, bytes.data(), bytes.size());
    const auto seq = ++lastStateRequestSeq_;
    stateState_->requestKind .store (PluginStateState::Load,
                                     std::memory_order_relaxed);
    stateState_->requestBytes.store (static_cast<std::uint32_t> (bytes.size()),
                                     std::memory_order_relaxed);
    stateState_->requestSeq  .store (seq, std::memory_order_release);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (stateState_->responseSeq.load (std::memory_order_acquire) >= seq)
            break;
        std::this_thread::sleep_for (kStatePollInterval);
    }

    if (stateState_->responseSeq.load (std::memory_order_acquire) < seq)
        return false; // timeout

    return stateState_->responseStatus.load (std::memory_order_relaxed)
           == PluginStateState::Ok;
}

} // namespace sirius
