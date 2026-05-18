// =============================================================================
// sirius_plugin_host — standalone child-process binary (M7 Session 1)
// =============================================================================
// V7 alignment plan Milestone 7, Session 1: the simplest transport before
// shared memory. This binary is the "synthetic plug-in" the S1 spec calls
// for — it reads bytes from stdin and writes them byte-identical to stdout.
// S2 replaces the stdin/stdout transport with POSIX shared-memory + SPSC
// rings; S3 wires the engine-side OutOfProcessPluginInstance through
// OutputMixer. No CLAP SDK yet (deferred to S2 per orchestrator decision).
//
// Deliberately JUCE-free and dependency-free. The host binary needs to be
// portable + light: the engine launches one per plug-in instance, so every
// kilobyte of startup cost compounds, and any link-time dependency the
// engine carries the host inherits.
// =============================================================================

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace
{
    /// Pipe-pump chunk size. 4 KiB matches a typical Darwin / Linux pipe
    /// page boundary; larger reads on a pipe just block until either the
    /// requested bytes arrive or the writer closes. Smaller wastes syscalls.
    constexpr std::size_t kPipeChunkBytes = 4096;

    /// Process exit codes. main() returns these — kept named so test
    /// assertions don't have to traffic in magic integers.
    constexpr int kExitOk            = 0;
    constexpr int kExitErr           = 1;
    constexpr int kExitBadArgs       = 2;
    constexpr int kExitUnknownMode   = 3;

    /// Set by the SIGTERM / SIGINT handlers so the pump loop notices the
    /// signal between reads and exits cleanly instead of being torn down
    /// mid-write. `volatile sig_atomic_t` is the only type the C++ standard
    /// guarantees an async signal handler may touch portably.
    volatile std::sig_atomic_t shouldExit = 0;

    extern "C" {
        // Declared with the same linkage spec as the definition so clang's
        // -Wmissing-prototypes is satisfied without exposing it outside this
        // anonymous namespace.
        void onTerminationSignal (int /*signal*/);
    }

    extern "C" void onTerminationSignal (int /*signal*/)
    {
        shouldExit = 1;
    }

    /// Writes `count` bytes from `data` to fd, retrying on partial writes
    /// and EINTR. Returns true on full delivery, false on a fatal write
    /// error (broken pipe, etc.).
    bool writeAll (int fd, const char* data, std::size_t count)
    {
        std::size_t written = 0;
        while (written < count)
        {
            const auto chunk = ::write (fd, data + written, count - written);
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

    /// Identity-mode pump: read up to kPipeChunkBytes from stdin, echo
    /// the exact bytes to stdout, flush, repeat until EOF or signal.
    int runIdentityMode()
    {
        char buffer[kPipeChunkBytes];

        while (shouldExit == 0)
        {
            const auto bytesRead = ::read (STDIN_FILENO, buffer, kPipeChunkBytes);
            if (bytesRead < 0)
            {
                if (errno == EINTR)
                    continue;
                // Fail loud per CLAUDE.md rule 8 — a real stdin read error
                // is genuinely unusual and the parent's supervisor needs to
                // observe a non-zero exit code to escalate. Only EOF
                // (bytesRead == 0) below counts as clean teardown.
                return kExitErr;
            }
            if (bytesRead == 0)
                return kExitOk; // parent closed our stdin — clean EOF.

            if (! writeAll (STDOUT_FILENO, buffer, static_cast<std::size_t> (bytesRead)))
                return kExitOk; // parent closed our stdout — clean teardown.
        }
        return kExitOk;
    }

    /// Parses `argv` looking for `--instance-id <value>` and `--mode <value>`.
    /// Both are required. instanceId is logged-only in S1 (S2 will use it as
    /// the shared-memory segment name). Returns true if both were present.
    bool parseArgs (int argc, char** argv, std::string& instanceId, std::string& mode)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            const auto flag = std::string (argv[i]);
            if (flag == "--instance-id")
                instanceId = argv[++i];
            else if (flag == "--mode")
                mode = argv[++i];
        }
        return ! instanceId.empty() && ! mode.empty();
    }
}

int main (int argc, char** argv)
{
    std::string instanceId;
    std::string mode;

    if (! parseArgs (argc, argv, instanceId, mode))
    {
        std::fprintf (stderr,
                      "sirius_plugin_host: required args missing\n"
                      "  usage: sirius_plugin_host --instance-id <id> --mode identity\n");
        return kExitBadArgs;
    }

    // Wire SIGTERM + SIGINT so the supervisor can ask us to leave without
    // the kernel having to send SIGKILL. SIGPIPE -> ignore: a writer that
    // dies mid-stream should surface as a write() error in the pump loop,
    // not a process-killing signal.
    std::signal (SIGTERM, onTerminationSignal);
    std::signal (SIGINT,  onTerminationSignal);
    std::signal (SIGPIPE, SIG_IGN);

    if (mode == "identity")
        return runIdentityMode();

    std::fprintf (stderr,
                  "sirius_plugin_host: unknown --mode '%s' (S1 only supports 'identity')\n",
                  mode.c_str());
    return kExitUnknownMode;
}
