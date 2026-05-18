// =============================================================================
// sirius_plugin_host — standalone child-process binary (M7 S1+)
// =============================================================================
// V7 alignment plan Milestone 7. The engine launches one of these per hosted
// plug-in instance; this binary loads the plug-in into its own address space
// and shuttles audio through it.
//
// Modes:
//   --mode identity                       — copy stdin → stdout (S1 path).
//   --mode clap --plugin-path <bundle>   — load a .clap bundle via dlopen +
//                                          pump audio buffers through its
//                                          process() callback (S2a).
//
// Deliberately JUCE-free: the engine spawns one of these per hosted plug-in,
// so link-time weight here multiplies. CLAP is header-only (`clap` target
// from external/clap is INTERFACE), and the only S2a runtime addition is
// `dl` on Linux. The shared-memory IPC swap-in (S2c) replaces this file's
// stdin/stdout transport without touching the CLAP loader.
//
// Wire format for both modes (stdin reads + stdout writes):
//   per audio buffer: uint32_t frameCount (host byte order),
//                     followed by frameCount × 2 × float (interleaved L,R)
// EOF on stdin = clean exit.
// =============================================================================

#include <clap/clap.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>
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
    constexpr int kExitClapLoadErr   = 4;

    /// CLAP plug-in audio buffer sizing — must accommodate the largest
    /// frameCount the engine may send. 1024 is the V7 plan's outer block-
    /// size envelope; if the engine ever sends more, the host re-allocates
    /// before processing. Two channels (stereo) fixed for S2a.
    constexpr uint32_t kInitialMaxFrames = 1024;
    constexpr uint32_t kChannels         = 2;

    /// CLAP activate() parameters — sample rate is informational for the
    /// identity plug-in, but real plug-ins need it. 48 kHz is the engine's
    /// canonical rate (LMC fundamental); deviate only when the engine
    /// negotiates differently.
    constexpr double   kSampleRate = 48000.0;

    /// Set by the SIGTERM / SIGINT handlers so the pump loop notices the
    /// signal between reads and exits cleanly instead of being torn down
    /// mid-write. `volatile sig_atomic_t` is the only type the C++ standard
    /// guarantees an async signal handler may touch portably.
    volatile std::sig_atomic_t shouldExit = 0;

    extern "C" {
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

    /// Reads exactly `count` bytes from fd into `data`. Returns:
    ///   true       — all bytes read,
    ///   false + eof=true — clean EOF before any data read,
    ///   false + eof=false — partial read then EOF, or read error.
    bool readAll (int fd, char* data, std::size_t count, bool& eof)
    {
        eof = false;
        std::size_t got = 0;
        while (got < count)
        {
            const auto chunk = ::read (fd, data + got, count - got);
            if (chunk < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (chunk == 0)
            {
                eof = (got == 0);
                return false;
            }
            got += static_cast<std::size_t> (chunk);
        }
        return true;
    }

    /// Identity-mode pump: read up to kPipeChunkBytes from stdin, echo
    /// the exact bytes to stdout, flush, repeat until EOF or signal.
    /// This mode does NOT use the framed wire format above — it's the
    /// byte-stream pass-through the S1 transport tests depend on.
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

    // -------------------------------------------------------------------------
    // CLAP mode
    // -------------------------------------------------------------------------

    /// On macOS, .clap is a bundle directory; the executable lives at
    /// `<bundle>/Contents/MacOS/<basename-without-.clap>`. We hand-construct
    /// that path so we can dlopen the inner binary without pulling in
    /// CoreFoundation just for CFBundle.
    std::string resolveClapBinaryPath (std::string path)
    {
       #ifdef __APPLE__
        while (! path.empty() && path.back() == '/')
            path.pop_back();
        const auto slash = path.find_last_of ('/');
        std::string basename = (slash == std::string::npos) ? path : path.substr (slash + 1);
        if (basename.size() >= 5
            && basename.compare (basename.size() - 5, 5, ".clap") == 0)
            basename.resize (basename.size() - 5);
        return path + "/Contents/MacOS/" + basename;
       #else
        return path;
       #endif
    }

    /// Minimal `clap_host` shim. The synthetic identity plug-in does not
    /// query any host extensions, but real plug-ins do — return null and
    /// let CLAP version negotiation do its job.
    const void* hostGetExtension (const clap_host_t*, const char* /*id*/)
    {
        return nullptr;
    }
    void hostRequestRestart  (const clap_host_t*) {}
    void hostRequestProcess  (const clap_host_t*) {}
    void hostRequestCallback (const clap_host_t*) {}

    clap_host_t makeHost()
    {
        clap_host_t host {};
        host.clap_version = CLAP_VERSION_INIT;
        host.host_data    = nullptr;
        host.name         = "sirius_plugin_host";
        host.vendor       = "Sirius Looper";
        host.url          = "https://example.invalid/sirius";
        host.version      = "0.1.0";
        host.get_extension   = hostGetExtension;
        host.request_restart = hostRequestRestart;
        host.request_process = hostRequestProcess;
        host.request_callback= hostRequestCallback;
        return host;
    }

    int runClapMode (const std::string& pluginPath)
    {
        const auto binaryPath = resolveClapBinaryPath (pluginPath);

        void* handle = ::dlopen (binaryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            std::fprintf (stderr,
                          "sirius_plugin_host: dlopen('%s') failed: %s\n",
                          binaryPath.c_str(), ::dlerror());
            return kExitClapLoadErr;
        }

        auto* entry = static_cast<const clap_plugin_entry_t*> (::dlsym (handle, "clap_entry"));
        if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version))
        {
            std::fprintf (stderr, "sirius_plugin_host: clap_entry missing or incompatible\n");
            ::dlclose (handle);
            return kExitClapLoadErr;
        }

        if (! entry->init (binaryPath.c_str()))
        {
            std::fprintf (stderr, "sirius_plugin_host: entry->init failed\n");
            ::dlclose (handle);
            return kExitClapLoadErr;
        }

        const auto* factory = static_cast<const clap_plugin_factory_t*> (
            entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
        if (factory == nullptr || factory->get_plugin_count (factory) == 0)
        {
            std::fprintf (stderr, "sirius_plugin_host: no CLAP plug-in factory\n");
            entry->deinit();
            ::dlclose (handle);
            return kExitClapLoadErr;
        }

        const auto* desc = factory->get_plugin_descriptor (factory, 0);
        if (desc == nullptr || desc->id == nullptr)
        {
            std::fprintf (stderr, "sirius_plugin_host: descriptor missing\n");
            entry->deinit();
            ::dlclose (handle);
            return kExitClapLoadErr;
        }

        auto host   = makeHost();
        const auto* plugin = factory->create_plugin (factory, &host, desc->id);
        if (plugin == nullptr || ! plugin->init (plugin))
        {
            std::fprintf (stderr, "sirius_plugin_host: create_plugin/init failed\n");
            entry->deinit();
            ::dlclose (handle);
            return kExitClapLoadErr;
        }

        if (! plugin->activate (plugin, kSampleRate, 1, kInitialMaxFrames))
        {
            std::fprintf (stderr, "sirius_plugin_host: plugin->activate failed\n");
            plugin->destroy (plugin);
            entry->deinit();
            ::dlclose (handle);
            return kExitClapLoadErr;
        }

        plugin->start_processing (plugin);

        // Channel-deinterleaved buffers for CLAP's process() call. Resized
        // on demand if the engine sends more frames than the current peak.
        std::vector<float>  inLeft  (kInitialMaxFrames);
        std::vector<float>  inRight (kInitialMaxFrames);
        std::vector<float>  outLeft (kInitialMaxFrames);
        std::vector<float>  outRight(kInitialMaxFrames);
        std::vector<float>  scratch (kInitialMaxFrames * kChannels);

        const auto ensureCapacity = [&] (uint32_t frames)
        {
            if (frames > inLeft.size())
            {
                inLeft .resize (frames);
                inRight.resize (frames);
                outLeft.resize (frames);
                outRight.resize(frames);
                scratch.resize (frames * kChannels);
                plugin->deactivate (plugin);
                plugin->activate   (plugin, kSampleRate, 1, frames);
                plugin->start_processing (plugin);
            }
        };

        // Stub event queues — the identity plug-in ignores them, but
        // process() will null-deref on a missing pointer.
        constexpr auto inEventsSize = +[] (const clap_input_events_t*) -> uint32_t { return 0; };
        constexpr auto inEventsGet  = +[] (const clap_input_events_t*, uint32_t)
                                          -> const clap_event_header_t* { return nullptr; };
        const clap_input_events_t inEvents { nullptr, inEventsSize, inEventsGet };

        constexpr auto outEventsTry = +[] (const clap_output_events_t*,
                                           const clap_event_header_t*) -> bool { return true; };
        const clap_output_events_t outEvents { nullptr, outEventsTry };

        int exitCode = kExitOk;

        while (shouldExit == 0)
        {
            uint32_t frameCount = 0;
            bool eof = false;
            if (! readAll (STDIN_FILENO,
                           reinterpret_cast<char*> (&frameCount),
                           sizeof (frameCount), eof))
            {
                exitCode = eof ? kExitOk : kExitErr;
                break;
            }
            if (frameCount == 0)
                continue;

            ensureCapacity (frameCount);

            // Read interleaved L,R floats from stdin into scratch, then
            // deinterleave into inLeft/inRight for CLAP's channel-per-buffer
            // layout.
            const auto interleavedBytes = frameCount * kChannels * sizeof (float);
            if (! readAll (STDIN_FILENO,
                           reinterpret_cast<char*> (scratch.data()),
                           interleavedBytes, eof))
            {
                exitCode = eof ? kExitOk : kExitErr;
                break;
            }
            for (uint32_t f = 0; f < frameCount; ++f)
            {
                inLeft [f] = scratch[f * kChannels + 0];
                inRight[f] = scratch[f * kChannels + 1];
            }

            float*       inChannels [kChannels] = { inLeft .data(), inRight.data() };
            float*       outChannels[kChannels] = { outLeft.data(), outRight.data() };

            const clap_audio_buffer_t audioIn = {
                inChannels, nullptr, kChannels, 0, 0
            };
            clap_audio_buffer_t audioOut = {
                outChannels, nullptr, kChannels, 0, 0
            };

            const clap_process_t process = {
                /*steady_time*/        0,
                /*frames_count*/       frameCount,
                /*transport*/          nullptr,
                /*audio_inputs*/       &audioIn,
                /*audio_outputs*/      &audioOut,
                /*audio_inputs_count*/ 1,
                /*audio_outputs_count*/1,
                /*in_events*/          &inEvents,
                /*out_events*/         &outEvents
            };

            (void) plugin->process (plugin, &process);

            // Re-interleave processed output and ship it.
            for (uint32_t f = 0; f < frameCount; ++f)
            {
                scratch[f * kChannels + 0] = outLeft [f];
                scratch[f * kChannels + 1] = outRight[f];
            }
            if (! writeAll (STDOUT_FILENO,
                            reinterpret_cast<const char*> (scratch.data()),
                            interleavedBytes))
            {
                exitCode = kExitOk; // parent closed stdout — clean teardown.
                break;
            }
        }

        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
        plugin->destroy (plugin);
        entry->deinit();
        ::dlclose (handle);
        return exitCode;
    }

    /// Parses `argv` looking for `--instance-id <value>`, `--mode <value>`,
    /// and optionally `--plugin-path <value>`. instance-id and mode are
    /// required for all modes; plugin-path is required for `--mode clap`.
    /// Returns true if required args were present.
    bool parseArgs (int argc, char** argv,
                    std::string& instanceId,
                    std::string& mode,
                    std::string& pluginPath)
    {
        for (int i = 1; i < argc - 1; ++i)
        {
            const auto flag = std::string (argv[i]);
            if      (flag == "--instance-id") instanceId = argv[++i];
            else if (flag == "--mode")        mode       = argv[++i];
            else if (flag == "--plugin-path") pluginPath = argv[++i];
        }
        return ! instanceId.empty() && ! mode.empty();
    }
}

int main (int argc, char** argv)
{
    std::string instanceId;
    std::string mode;
    std::string pluginPath;

    if (! parseArgs (argc, argv, instanceId, mode, pluginPath))
    {
        std::fprintf (stderr,
                      "sirius_plugin_host: required args missing\n"
                      "  usage: sirius_plugin_host --instance-id <id> --mode identity\n"
                      "         sirius_plugin_host --instance-id <id> --mode clap --plugin-path <bundle>\n");
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

    if (mode == "clap")
    {
        if (pluginPath.empty())
        {
            std::fprintf (stderr,
                          "sirius_plugin_host: --mode clap requires --plugin-path <bundle>\n");
            return kExitBadArgs;
        }
        return runClapMode (pluginPath);
    }

    std::fprintf (stderr,
                  "sirius_plugin_host: unknown --mode '%s' (supported: identity, clap)\n",
                  mode.c_str());
    return kExitUnknownMode;
}
