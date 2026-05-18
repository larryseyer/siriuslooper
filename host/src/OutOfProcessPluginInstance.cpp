#include "sirius/OutOfProcessPluginInstance.h"

#include <juce_core/juce_core.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace sirius
{

namespace
{
    /// Index aliases for the int[2] arrays POSIX `pipe()` returns. Reads
    /// happen from kReadEnd; writes to kWriteEnd. Named so the code below
    /// doesn't read like "pipe[0]" magic numbers.
    constexpr int kReadEnd  = 0;
    constexpr int kWriteEnd = 1;

    /// Poll interval used while waiting for the child to exit during
    /// shutdown(). Short enough that a clean exit is observed promptly,
    /// long enough that we don't burn the CPU spinning on waitpid().
    constexpr int kShutdownPollMs = 5;

    /// Closes a file descriptor if it is currently open. Sets it to -1
    /// so repeat calls are no-ops. `noexcept` — used from destructors.
    void closeIfOpen (int& fd) noexcept
    {
        if (fd >= 0)
        {
            ::close (fd);
            fd = -1;
        }
    }
}

OutOfProcessPluginInstance::OutOfProcessPluginInstance (const juce::File& hostBinaryPath,
                                                        std::string instanceId)
    : instanceId_ (std::move (instanceId))
{
    int stdinPipe[2]  = { -1, -1 };
    int stdoutPipe[2] = { -1, -1 };

    if (::pipe (stdinPipe) != 0 || ::pipe (stdoutPipe) != 0)
    {
        closeIfOpen (stdinPipe[kReadEnd]);
        closeIfOpen (stdinPipe[kWriteEnd]);
        closeIfOpen (stdoutPipe[kReadEnd]);
        closeIfOpen (stdoutPipe[kWriteEnd]);
        return; // childPid_ stays -1; isRunning() will report false.
    }

    const auto binaryPath = hostBinaryPath.getFullPathName().toStdString();

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close (stdinPipe[kReadEnd]);
        ::close (stdinPipe[kWriteEnd]);
        ::close (stdoutPipe[kReadEnd]);
        ::close (stdoutPipe[kWriteEnd]);
        return;
    }

    if (pid == 0)
    {
        // Child: rewire stdin/stdout to the pipes the parent will speak
        // over, close every other inherited fd we know about, exec the
        // host binary. Anything that goes wrong below kills the child
        // with _exit() — never propagate errors up via exceptions or
        // atexit handlers from a fork()ed context.
        if (::dup2 (stdinPipe[kReadEnd],   STDIN_FILENO)  < 0) ::_exit (127);
        if (::dup2 (stdoutPipe[kWriteEnd], STDOUT_FILENO) < 0) ::_exit (127);

        ::close (stdinPipe[kReadEnd]);
        ::close (stdinPipe[kWriteEnd]);
        ::close (stdoutPipe[kReadEnd]);
        ::close (stdoutPipe[kWriteEnd]);

        // execvp() wants a NUL-terminated argv. Strings live for the
        // duration of the call only — execvp either replaces this
        // process image or returns on failure.
        std::vector<std::string> argvStorage;
        argvStorage.reserve (5);
        argvStorage.push_back (binaryPath);
        argvStorage.emplace_back ("--instance-id");
        argvStorage.push_back (instanceId_);
        argvStorage.emplace_back ("--mode");
        argvStorage.emplace_back ("identity");

        std::vector<char*> argv;
        argv.reserve (argvStorage.size() + 1);
        for (auto& s : argvStorage)
            argv.push_back (s.data());
        argv.push_back (nullptr);

        ::execvp (binaryPath.c_str(), argv.data());
        ::_exit (127); // exec failed
    }

    // Parent: keep the parent-side ends, close the child-side ends so the
    // child sees a clean EOF when we eventually close our write end.
    ::close (stdinPipe[kReadEnd]);
    ::close (stdoutPipe[kWriteEnd]);

    stdinWriteFd_ = stdinPipe[kWriteEnd];
    stdoutReadFd_ = stdoutPipe[kReadEnd];
    childPid_     = static_cast<int> (pid);
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
    if (stdinWriteFd_ < 0 || data == nullptr)
        return false;

    std::size_t written = 0;
    while (written < count)
    {
        const auto chunk = ::write (stdinWriteFd_,
                                    data + written,
                                    count - written);
        if (chunk < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (chunk == 0)
            return false;
        written += static_cast<std::size_t> (chunk);
    }
    return true;
}

std::size_t OutOfProcessPluginInstance::readBytes (std::byte* buffer,
                                                   std::size_t capacity,
                                                   int timeoutMs)
{
    if (stdoutReadFd_ < 0 || buffer == nullptr || capacity == 0)
        return 0;

    using clock = std::chrono::steady_clock;
    const auto deadline = (timeoutMs < 0)
                              ? clock::time_point::max()
                              : clock::now() + std::chrono::milliseconds (timeoutMs);

    std::size_t total = 0;
    while (total < capacity)
    {
        int pollWaitMs = -1;
        if (timeoutMs >= 0)
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (
                                       deadline - clock::now()).count();
            if (remaining <= 0)
                break;
            pollWaitMs = static_cast<int> (remaining);
        }

        pollfd pfd { stdoutReadFd_, POLLIN, 0 };
        const int p = ::poll (&pfd, 1, pollWaitMs);
        if (p < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (p == 0)
            break; // timeout

        const auto chunk = ::read (stdoutReadFd_,
                                   buffer + total,
                                   capacity - total);
        if (chunk < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        if (chunk == 0)
            break; // EOF
        total += static_cast<std::size_t> (chunk);

        // Stop after the first non-empty read so the caller can decide
        // whether to wait for more. Otherwise a slow producer forces us
        // to block until either capacity is filled or the timeout
        // elapses, which is rarely what the caller wants.
        if (total > 0)
            break;
    }
    return total;
}

void OutOfProcessPluginInstance::closeStdinWrite() noexcept
{
    closeIfOpen (stdinWriteFd_);
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

    // Close stdin first — that's the polite "we're done" signal the host
    // binary's identity loop reads as EOF and exits on.
    closeStdinWrite();

    if (childPid_ < 0)
    {
        closeIfOpen (stdoutReadFd_);
        return;
    }

    // Wait up to kShutdownGraceMs for a clean exit before escalating.
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
        // Child is still alive past the grace period — send SIGTERM
        // (asks for a clean exit), then SIGKILL as a hard backstop.
        ::kill (childPid_, SIGTERM);
        const auto killDeadline = clock::now() + std::chrono::milliseconds (kShutdownGraceMs);
        while (clock::now() < killDeadline)
        {
            if (reapIfExited())
                break;
            ::usleep (kShutdownPollMs * 1000);
        }

        if (childPid_ >= 0)
        {
            ::kill (childPid_, SIGKILL);
            // Blocking wait — the kernel guarantees SIGKILL is honored,
            // so this returns promptly.
            int status = 0;
            ::waitpid (childPid_, &status, 0);
            childPid_ = -1;
        }
    }

    closeIfOpen (stdoutReadFd_);
}

} // namespace sirius
