// =============================================================================
// sirius_plugin_host — standalone child-process binary (M7 S1–S2c)
// =============================================================================
// V7 alignment plan Milestone 7. The engine launches one of these per hosted
// plug-in instance; this binary loads the plug-in into its own address space
// and shuttles audio through it.
//
// Modes:
//   --mode identity                       — copy engine→host ring messages
//                                          straight back into the host→engine
//                                          ring (byte-stream pass-through).
//   --mode clap --plugin-path <bundle>   — load a .clap bundle via dlopen +
//                                          pump audio buffers through its
//                                          process() callback.
//
// S2c transport: POSIX shared-memory SPSC rings (one engine→host, one
// host→engine), opened by the child via shm names derived from --instance-id.
// CLAP is header-only (`clap` target from external/clap is INTERFACE), and
// the only S2 runtime addition is `dl` on Linux.
//
// Wire format inside the rings:
//   - identity mode: each ring carries `PluginIpcMessage` records; the host
//     pops one and pushes the same payload back as a single record.
//   - CLAP mode: each engine→host ring record is a fragment of the framed
//     stream `uint32_t frameCount, frameCount × 2 × float (L,R)`; the host
//     drains via RingByteStream::readExact, processes, and writes the
//     output stream back through RingByteStream::writeAll + flush.
// =============================================================================

#include "sirius/PluginGuiState.h"
#include "sirius/PluginIpcMessage.h"
#include "sirius/SharedMemoryRegion.h"
#include "sirius/SharedMemorySpscQueue.h"

#include <clap/clap.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <unistd.h>

namespace
{
    /// Process exit codes. main() returns these — kept named so test
    /// assertions don't have to traffic in magic integers.
    constexpr int kExitOk            = 0;
    constexpr int kExitErr           = 1;
    constexpr int kExitBadArgs       = 2;
    constexpr int kExitUnknownMode   = 3;
    constexpr int kExitClapLoadErr   = 4;
    constexpr int kExitShmErr        = 5;

    /// CLAP plug-in audio buffer sizing — must accommodate the largest
    /// frameCount the engine may send. 1024 is the V7 plan's outer block-
    /// size envelope; if the engine ever sends more, the host re-activates
    /// before processing. Two channels (stereo) fixed for S2c.
    constexpr uint32_t kInitialMaxFrames = 1024;
    constexpr uint32_t kChannels         = 2;
    constexpr double   kSampleRate       = 48000.0;

    /// Ring poll backoff — matches the engine-side cadence in
    /// OutOfProcessPluginInstance. Short spin + brief sleep keeps round-
    /// trip latency in the µs range while still yielding the CPU when
    /// nothing's in flight.
    constexpr int kRingSpinIterations    = 32;
    constexpr int kRingPollMicroseconds  = 50;

    /// Bound on push retries when the host→engine ring is full. 100 ms is
    /// the same as the engine side — exists to prevent a stuck pump from
    /// holding a CPU forever; in practice the engine drains continuously.
    constexpr int kRingPushTimeoutMs = 100;

    /// Set by the SIGTERM / SIGINT handlers so pump loops notice the
    /// signal between iterations and exit cleanly. `volatile sig_atomic_t`
    /// is the only type the C++ standard guarantees an async signal
    /// handler may touch portably.
    volatile std::sig_atomic_t shouldExit = 0;

    extern "C" {
        void onTerminationSignal (int /*signal*/);
    }

    extern "C" void onTerminationSignal (int /*signal*/)
    {
        shouldExit = 1;
    }

    using IpcQueue = sirius::SharedMemorySpscQueue<sirius::PluginIpcMessage>;

   #ifdef __APPLE__
    extern "C" std::uint32_t sirius_gui_show   (const clap_plugin_t*,
                                                std::uint32_t, std::uint32_t);
    extern "C" bool          sirius_gui_hide   (const clap_plugin_t*);
    extern "C" std::uint32_t sirius_gui_resize (const clap_plugin_t*,
                                                std::uint32_t, std::uint32_t);
   #endif

    /// State the CLAP-mode pump carries for the M7 S5 GUI-control region.
    /// The engine creates `/sirius.<id>.gui` before fork; the child
    /// attaches in main() and threads this struct through the pump loop.
    /// `lastServicedSeq` lives here (not in PluginGuiState) so it's a
    /// child-private cursor, not shared state.
    struct GuiServicingState
    {
        sirius::PluginGuiState* state           { nullptr };
        std::uint64_t           lastServicedSeq { 0 };
    };

    /// Services any outstanding GUI request on `gui`. No-op on platforms
    /// without GUI support OR when no request has advanced past
    /// `lastServicedSeq`. Idempotent — safe to call from inside polling
    /// loops AND from the per-buffer servicing point.
    void serviceGuiRequests (GuiServicingState& gui, const clap_plugin_t* plugin)
    {
        if (gui.state == nullptr)
            return;
        const auto current = gui.state->requestSeq.load (std::memory_order_acquire);
        if (current == gui.lastServicedSeq)
            return;

        const auto kind   = gui.state->requestKind  .load (std::memory_order_relaxed);
        const auto width  = gui.state->requestWidth .load (std::memory_order_relaxed);
        const auto height = gui.state->requestHeight.load (std::memory_order_relaxed);

        std::uint32_t contextId = 0;
        std::uint32_t outWidth  = width;
        std::uint32_t outHeight = height;

       #ifdef __APPLE__
        switch (kind)
        {
            case sirius::PluginGuiState::Show:
                contextId = sirius_gui_show (plugin, width, height);
                break;
            case sirius::PluginGuiState::Hide:
                sirius_gui_hide (plugin);
                contextId = 0;
                outWidth  = 0;
                outHeight = 0;
                break;
            case sirius::PluginGuiState::Resize:
                contextId = sirius_gui_resize (plugin, width, height);
                break;
            case sirius::PluginGuiState::None:
            default:
                break;
        }
       #else
        (void) plugin; (void) kind;
        contextId = 0;
        outWidth  = 0;
        outHeight = 0;
       #endif

        gui.state->responseContextId.store (contextId, std::memory_order_relaxed);
        gui.state->responseWidth    .store (outWidth,  std::memory_order_relaxed);
        gui.state->responseHeight   .store (outHeight, std::memory_order_relaxed);
        gui.state->responseSeq      .store (current,   std::memory_order_release);
        gui.lastServicedSeq = current;
    }

    /// Pops one PluginIpcMessage from `q`, honoring `shouldExit`. If
    /// `onSleep` is non-null it runs once per sleep iteration — used by
    /// CLAP mode to service GUI requests while idle. Returns false only
    /// when the loop was asked to exit; callers should treat that as
    /// "no more work, time to leave."
    template <typename OnSleep>
    bool popMessageBlocking (IpcQueue& q,
                             sirius::PluginIpcMessage& out,
                             OnSleep&& onSleep)
    {
        while (shouldExit == 0)
        {
            for (int i = 0; i < kRingSpinIterations; ++i)
            {
                if (q.pop (out))
                    return true;
            }
            onSleep();
            ::usleep (kRingPollMicroseconds);
        }
        return false;
    }

    /// No-op overload for the identity mode that has no GUI side work.
    bool popMessageBlocking (IpcQueue& q, sirius::PluginIpcMessage& out)
    {
        return popMessageBlocking (q, out, [] {});
    }

    /// Pushes one PluginIpcMessage onto `q`, retrying briefly if the ring
    /// is full. Returns false on shutdown or sustained full-ring stall.
    bool pushMessageBlocking (IpcQueue& q, const sirius::PluginIpcMessage& msg)
    {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds (kRingPushTimeoutMs);
        while (shouldExit == 0)
        {
            if (q.push (msg))
                return true;
            if (clock::now() >= deadline)
                return false;
            ::usleep (kRingPollMicroseconds);
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // RingByteStream — turns the two SPSC rings into a familiar stream
    // pair (readExact / writeAll / flush). Used by CLAP-mode; identity-mode
    // operates directly on messages because it never re-packages payloads.
    // -------------------------------------------------------------------------
    class RingByteStream
    {
    public:
        using OnIdle = std::function<void()>;

        RingByteStream (IpcQueue& inRing, IpcQueue& outRing, OnIdle onIdle = {})
            : inRing_ (inRing), outRing_ (outRing), onIdle_ (std::move (onIdle)) {}

        /// Reads exactly `count` bytes into `data`. Returns false if the
        /// loop was asked to exit before the read completed; eof flag
        /// distinguishes clean-shutdown from short-read.
        bool readExact (char* data, std::size_t count, bool& eof)
        {
            eof = false;
            std::size_t got = 0;
            while (got < count)
            {
                if (readCursor_ < pendingRead_.payloadBytes)
                {
                    const std::size_t take = std::min<std::size_t> (
                        count - got, pendingRead_.payloadBytes - readCursor_);
                    std::memcpy (data + got, pendingRead_.payload + readCursor_, take);
                    readCursor_ += take;
                    got += take;
                    continue;
                }
                bool ok;
                if (onIdle_)
                    ok = popMessageBlocking (inRing_, pendingRead_, onIdle_);
                else
                    ok = popMessageBlocking (inRing_, pendingRead_);
                if (! ok)
                {
                    eof = (got == 0);
                    return false;
                }
                readCursor_ = 0;
            }
            return true;
        }

        /// Buffers `count` bytes into pendingWrite_, splitting across
        /// messages as needed; pushes whenever a message fills.
        bool writeAll (const char* data, std::size_t count)
        {
            while (count > 0)
            {
                if (pendingWrite_.payloadBytes >= sirius::PluginIpcMessage::kMaxPayloadBytes)
                    if (! flush())
                        return false;

                const std::size_t room = sirius::PluginIpcMessage::kMaxPayloadBytes
                                       - pendingWrite_.payloadBytes;
                const std::size_t take = std::min (count, room);
                std::memcpy (pendingWrite_.payload + pendingWrite_.payloadBytes,
                             data, take);
                pendingWrite_.payloadBytes += static_cast<std::uint32_t> (take);
                data  += take;
                count -= take;
            }
            return true;
        }

        /// Pushes whatever's currently buffered in pendingWrite_.
        bool flush()
        {
            if (pendingWrite_.payloadBytes == 0)
                return true;

            pendingWrite_.kind        = sirius::PluginIpcMessage::Bytes;
            pendingWrite_.monotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds> (
                std::chrono::steady_clock::now().time_since_epoch()).count();

            const bool ok = pushMessageBlocking (outRing_, pendingWrite_);
            pendingWrite_ = {};
            return ok;
        }

    private:
        IpcQueue& inRing_;
        IpcQueue& outRing_;
        OnIdle    onIdle_;
        sirius::PluginIpcMessage pendingRead_  {};
        std::size_t              readCursor_   { 0 };
        sirius::PluginIpcMessage pendingWrite_ {};
    };

    // -------------------------------------------------------------------------
    // Identity mode: pop a message, echo the same payload back.
    // -------------------------------------------------------------------------
    int runIdentityMode (IpcQueue& inRing, IpcQueue& outRing)
    {
        sirius::PluginIpcMessage msg {};
        while (shouldExit == 0)
        {
            if (! popMessageBlocking (inRing, msg))
                return kExitOk;

            // Refresh the timestamp on the echo path so the round-trip
            // latency tests can measure (push-time → echo-time) deltas.
            msg.monotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds> (
                std::chrono::steady_clock::now().time_since_epoch()).count();

            if (! pushMessageBlocking (outRing, msg))
                return kExitOk;
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

    /// Minimal `clap_host` shim. Synthetic identity does not query host
    /// extensions; real plug-ins do — return null and let version
    /// negotiation do its job.
    const void* hostGetExtension (const clap_host_t*, const char*) { return nullptr; }
    void hostRequestRestart      (const clap_host_t*) {}
    void hostRequestProcess      (const clap_host_t*) {}
    void hostRequestCallback     (const clap_host_t*) {}

    clap_host_t makeHost()
    {
        clap_host_t host {};
        host.clap_version    = CLAP_VERSION_INIT;
        host.host_data       = nullptr;
        host.name            = "sirius_plugin_host";
        host.vendor          = "Sirius Looper";
        host.url             = "https://example.invalid/sirius";
        host.version         = "0.1.0";
        host.get_extension   = hostGetExtension;
        host.request_restart = hostRequestRestart;
        host.request_process = hostRequestProcess;
        host.request_callback= hostRequestCallback;
        return host;
    }

    int runClapMode (const std::string& pluginPath,
                     IpcQueue& inRing, IpcQueue& outRing,
                     sirius::PluginGuiState* guiState)
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

        auto host = makeHost();
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

        std::vector<float> inLeft  (kInitialMaxFrames);
        std::vector<float> inRight (kInitialMaxFrames);
        std::vector<float> outLeft (kInitialMaxFrames);
        std::vector<float> outRight(kInitialMaxFrames);
        std::vector<float> scratch (kInitialMaxFrames * kChannels);

        const auto ensureCapacity = [&] (uint32_t frames)
        {
            if (frames > inLeft.size())
            {
                inLeft  .resize (frames);
                inRight .resize (frames);
                outLeft .resize (frames);
                outRight.resize (frames);
                scratch .resize (frames * kChannels);
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

        GuiServicingState gui { guiState, 0 };
        const auto serviceGui = [&] { serviceGuiRequests (gui, plugin); };
        RingByteStream stream (inRing, outRing, serviceGui);
        int exitCode = kExitOk;

        while (shouldExit == 0)
        {
            // Service any GUI request that landed since the last buffer.
            // Bounds GUI latency to one audio buffer when audio is
            // flowing; the in-pop onIdle hook covers the idle case.
            serviceGuiRequests (gui, plugin);
            uint32_t frameCount = 0;
            bool eof = false;
            if (! stream.readExact (reinterpret_cast<char*> (&frameCount),
                                    sizeof (frameCount), eof))
            {
                exitCode = eof ? kExitOk : kExitErr;
                break;
            }
            if (frameCount == 0)
                continue;

            ensureCapacity (frameCount);

            const auto interleavedBytes = frameCount * kChannels * sizeof (float);
            if (! stream.readExact (reinterpret_cast<char*> (scratch.data()),
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

            const clap_audio_buffer_t audioIn  = { inChannels,  nullptr, kChannels, 0, 0 };
            clap_audio_buffer_t       audioOut = { outChannels, nullptr, kChannels, 0, 0 };

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

            for (uint32_t f = 0; f < frameCount; ++f)
            {
                scratch[f * kChannels + 0] = outLeft [f];
                scratch[f * kChannels + 1] = outRight[f];
            }
            if (! stream.writeAll (reinterpret_cast<const char*> (scratch.data()),
                                   interleavedBytes))
            {
                exitCode = kExitOk; // engine asked us to leave
                break;
            }
            if (! stream.flush())
            {
                exitCode = kExitOk;
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

    /// Parses `argv`. Required: --instance-id, --mode. CLAP mode adds
    /// --plugin-path. Returns true on success.
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

    // SIGTERM / SIGINT → request shutdown. SIGPIPE → ignore (legacy from
    // the S1 pipe transport; harmless to keep).
    std::signal (SIGTERM, onTerminationSignal);
    std::signal (SIGINT,  onTerminationSignal);
    std::signal (SIGPIPE, SIG_IGN);

    // Attach to the rings the engine created before forking us.
    std::unique_ptr<sirius::SharedMemoryRegion> e2hRegion;
    std::unique_ptr<sirius::SharedMemoryRegion> h2eRegion;
    std::unique_ptr<sirius::SharedMemoryRegion> guiRegion;
    std::unique_ptr<IpcQueue> e2hQueue;
    std::unique_ptr<IpcQueue> h2eQueue;
    sirius::PluginGuiState*   guiState = nullptr;
    try
    {
        e2hRegion = std::make_unique<sirius::SharedMemoryRegion> (
            sirius::makeEngineToHostRingName (instanceId), 0,
            sirius::SharedMemoryRegion::Mode::OpenExisting);
        h2eRegion = std::make_unique<sirius::SharedMemoryRegion> (
            sirius::makeHostToEngineRingName (instanceId), 0,
            sirius::SharedMemoryRegion::Mode::OpenExisting);
        e2hQueue = std::make_unique<IpcQueue> (
            IpcQueue::attach (e2hRegion->data(), sirius::kPluginIpcRingCapacity));
        h2eQueue = std::make_unique<IpcQueue> (
            IpcQueue::attach (h2eRegion->data(), sirius::kPluginIpcRingCapacity));

        // M7 S5 — open the GUI state region. The engine created it
        // alongside the audio rings before fork; this is OpenExisting.
        guiRegion = std::make_unique<sirius::SharedMemoryRegion> (
            sirius::makeGuiStateRegionName (instanceId), 0,
            sirius::SharedMemoryRegion::Mode::OpenExisting);
        guiState = sirius::PluginGuiState::view (guiRegion->data());
    }
    catch (const std::exception& e)
    {
        std::fprintf (stderr, "sirius_plugin_host: shm attach failed: %s\n", e.what());
        return kExitShmErr;
    }

    if (mode == "identity")
        return runIdentityMode (*e2hQueue, *h2eQueue);

    if (mode == "clap")
    {
        if (pluginPath.empty())
        {
            std::fprintf (stderr,
                          "sirius_plugin_host: --mode clap requires --plugin-path <bundle>\n");
            return kExitBadArgs;
        }
        return runClapMode (pluginPath, *e2hQueue, *h2eQueue, guiState);
    }

    std::fprintf (stderr,
                  "sirius_plugin_host: unknown --mode '%s' (supported: identity, clap)\n",
                  mode.c_str());
    return kExitUnknownMode;
}
