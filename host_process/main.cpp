// =============================================================================
// ida_plugin_host — standalone child-process binary (M7 S1–S2c)
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

#include "ida/ClapBundleLoader.h"
#include "ida/PluginGuiState.h"
#include "ida/PluginIpcMessage.h"
#include "ida/PluginStateRegion.h"
#include "ida/SharedMemoryRegion.h"
#include "ida/SharedMemorySpscQueue.h"

#include <clap/clap.h>
#include <clap/ext/state.h>

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

    using IpcQueue = ida::SharedMemorySpscQueue<ida::PluginIpcMessage>;

   #ifdef __APPLE__
    extern "C" std::uint32_t sirius_gui_show   (const clap_plugin_t*,
                                                std::uint32_t, std::uint32_t);
    extern "C" bool          sirius_gui_hide   (const clap_plugin_t*);
    extern "C" std::uint32_t sirius_gui_resize (const clap_plugin_t*,
                                                std::uint32_t, std::uint32_t);
    /// M7 S9 — wired in gui_cocoa.mm. `init` brings AppKit online
    /// (accessory activation policy, finishLaunching); `drain_events`
    /// non-blocking-pumps pending NSWindow input events; `set_state`
    /// hands the PluginGuiState shm pointer to the NSWindowDelegate so
    /// user-driven close can publish responseContextId=0 back to the
    /// engine without going through the request/response IPC path.
    extern "C" void sirius_appkit_init         (void);
    extern "C" void sirius_appkit_drain_events (void);
    extern "C" void sirius_gui_set_state       (ida::PluginGuiState* state);
   #endif

    /// State the CLAP-mode pump carries for the M7 S5 GUI-control region.
    /// The engine creates `/sirius.<id>.gui` before fork; the child
    /// attaches in main() and threads this struct through the pump loop.
    /// `lastServicedSeq` lives here (not in PluginGuiState) so it's a
    /// child-private cursor, not shared state.
    struct GuiServicingState
    {
        ida::PluginGuiState* state           { nullptr };
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
            case ida::PluginGuiState::Show:
                contextId = sirius_gui_show (plugin, width, height);
                break;
            case ida::PluginGuiState::Hide:
                sirius_gui_hide (plugin);
                contextId = 0;
                outWidth  = 0;
                outHeight = 0;
                break;
            case ida::PluginGuiState::Resize:
                contextId = sirius_gui_resize (plugin, width, height);
                break;
            case ida::PluginGuiState::None:
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

    /// State the CLAP-mode pump carries for the M8 S2 plug-in-state
    /// region. The engine creates `/sirius.<id>.state` before fork (once
    /// Task 7 lands); the child attaches in main() and threads this struct
    /// through the pump loop. `lastServicedSeq` is a child-private cursor,
    /// not shared state — mirrors GuiServicingState.
    struct StateServicingState
    {
        ida::PluginStateState* state           { nullptr };
        std::uint64_t             lastServicedSeq { 0 };
    };

    /// `clap_ostream_t` shim backed by a fixed-size char buffer. `write`
    /// appends bytes up to the buffer cap; over-cap writes return -1 so
    /// the plug-in's save callback knows to fail.
    struct OstreamBuf
    {
        char*       data;
        std::size_t cap;
        std::size_t used { 0 };
    };
    std::int64_t ostreamWrite (const clap_ostream_t* s, const void* in, std::uint64_t n)
    {
        auto* buf = static_cast<OstreamBuf*> (s->ctx);
        if (buf->used + n > buf->cap) return -1;
        std::memcpy (buf->data + buf->used, in, n);
        buf->used += static_cast<std::size_t> (n);
        return static_cast<std::int64_t> (n);
    }

    /// `clap_istream_t` shim backed by a fixed-size char buffer. `read`
    /// hands out up to the remaining bytes; the plug-in's load callback
    /// drains until it returns 0.
    struct IstreamBuf
    {
        const char* data;
        std::size_t cap;
        std::size_t pos { 0 };
    };
    std::int64_t istreamRead (const clap_istream_t* s, void* out, std::uint64_t n)
    {
        auto* buf = static_cast<IstreamBuf*> (s->ctx);
        const auto avail = buf->cap - buf->pos;
        const auto take  = static_cast<std::size_t> (
            n < avail ? n : avail);
        std::memcpy (out, buf->data + buf->pos, take);
        buf->pos += take;
        return static_cast<std::int64_t> (take);
    }

    /// Services any outstanding state request on `state`. No-op when the
    /// region is absent (engine versions before M8 S2 don't create it) OR
    /// when no request has advanced past `lastServicedSeq`. Publishes the
    /// response fields then bumps `responseSeq` with release ordering so
    /// the engine's acquire-load sees a consistent response. Idempotent.
    void serviceStateRequests (StateServicingState& state, const clap_plugin_t* plugin)
    {
        if (state.state == nullptr) return;
        const auto current = state.state->requestSeq.load (std::memory_order_acquire);
        if (current == state.lastServicedSeq) return;

        const auto kind = state.state->requestKind.load (std::memory_order_relaxed);

        state.state->responseBytes.store (0, std::memory_order_relaxed);
        state.state->responseStatus.store (ida::PluginStateState::Ok,
                                           std::memory_order_relaxed);

        const auto* ext = static_cast<const clap_plugin_state_t*> (
            plugin->get_extension (plugin, CLAP_EXT_STATE));
        if (ext == nullptr)
        {
            state.state->responseStatus.store (
                ida::PluginStateState::ErrorNotSupported,
                std::memory_order_relaxed);
            state.state->responseSeq.store (current, std::memory_order_release);
            state.lastServicedSeq = current;
            return;
        }

        if (kind == ida::PluginStateState::Save)
        {
            OstreamBuf out { state.state->responsePayload,
                             ida::PluginStateState::kMaxStateBytes,
                             0 };
            clap_ostream_t stream { &out, ostreamWrite };
            const bool ok = ext->save (plugin, &stream);
            if (! ok)
            {
                state.state->responseStatus.store (
                    ida::PluginStateState::ErrorGeneric,
                    std::memory_order_relaxed);
                state.state->responseBytes.store (0, std::memory_order_relaxed);
            }
            else
            {
                state.state->responseBytes.store (
                    static_cast<std::uint32_t> (out.used),
                    std::memory_order_relaxed);
            }
        }
        else if (kind == ida::PluginStateState::Load)
        {
            const auto n = state.state->requestBytes.load (std::memory_order_relaxed);
            if (n > ida::PluginStateState::kMaxStateBytes)
            {
                state.state->responseStatus.store (
                    ida::PluginStateState::ErrorTooLarge,
                    std::memory_order_relaxed);
            }
            else
            {
                IstreamBuf in { state.state->requestPayload, n, 0 };
                clap_istream_t stream { &in, istreamRead };
                const bool ok = ext->load (plugin, &stream);
                state.state->responseStatus.store (
                    ok ? ida::PluginStateState::Ok
                       : ida::PluginStateState::ErrorGeneric,
                    std::memory_order_relaxed);
            }
        }

        state.state->responseSeq.store (current, std::memory_order_release);
        state.lastServicedSeq = current;
    }

    /// Pops one PluginIpcMessage from `q`, honoring `shouldExit`. If
    /// `onSleep` is non-null it runs once per sleep iteration — used by
    /// CLAP mode to service GUI requests while idle. Returns false only
    /// when the loop was asked to exit; callers should treat that as
    /// "no more work, time to leave."
    template <typename OnSleep>
    bool popMessageBlocking (IpcQueue& q,
                             ida::PluginIpcMessage& out,
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
    bool popMessageBlocking (IpcQueue& q, ida::PluginIpcMessage& out)
    {
        return popMessageBlocking (q, out, [] {});
    }

    /// Pushes one PluginIpcMessage onto `q`, retrying briefly if the ring
    /// is full. Returns false on shutdown or sustained full-ring stall.
    bool pushMessageBlocking (IpcQueue& q, const ida::PluginIpcMessage& msg)
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
                if (pendingWrite_.payloadBytes >= ida::PluginIpcMessage::kMaxPayloadBytes)
                    if (! flush())
                        return false;

                const std::size_t room = ida::PluginIpcMessage::kMaxPayloadBytes
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

            pendingWrite_.kind        = ida::PluginIpcMessage::Bytes;
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
        ida::PluginIpcMessage pendingRead_  {};
        std::size_t              readCursor_   { 0 };
        ida::PluginIpcMessage pendingWrite_ {};
    };

    // -------------------------------------------------------------------------
    // Identity mode: pop a message, echo the same payload back.
    // -------------------------------------------------------------------------
    int runIdentityMode (IpcQueue& inRing, IpcQueue& outRing)
    {
        ida::PluginIpcMessage msg {};
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
        host.name            = "ida_plugin_host";
        host.vendor          = "IDA";
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
                     ida::PluginGuiState* guiState,
                     ida::PluginStateState* stateState)
    {
        std::string loadErr;
        auto loader = ida::ClapBundleLoader::load (pluginPath, loadErr);
        if (! loader.valid())
        {
            std::fprintf (stderr,
                          "ida_plugin_host: %s\n",
                          loadErr.c_str());
            return kExitClapLoadErr;
        }

        // First descriptor — synthetic CLAP exports exactly one; for shells
        // (e.g. Surge XT) we'd need a `--plugin-id` arg, but M8 S2's child
        // process is launched against a known single-plug-in bundle per slot.
        auto host = makeHost();
        const auto descriptors = loader.descriptors (pluginPath);
        if (descriptors.empty())
        {
            std::fprintf (stderr, "ida_plugin_host: bundle exports no plug-ins\n");
            return kExitClapLoadErr;
        }
        const auto* plugin = loader.createPlugin (host, descriptors.front().uniqueId.c_str());
        if (plugin == nullptr || ! plugin->init (plugin))
        {
            std::fprintf (stderr, "ida_plugin_host: create_plugin/init failed\n");
            return kExitClapLoadErr;
        }
        if (! plugin->activate (plugin, kSampleRate, 1, kInitialMaxFrames))
        {
            std::fprintf (stderr, "ida_plugin_host: plugin->activate failed\n");
            plugin->destroy (plugin);
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

        GuiServicingState   gui      { guiState, 0 };
        StateServicingState stateSrv { stateState, 0 };
       #ifdef __APPLE__
        // M7 S9 — hand the shm pointer to gui_cocoa.mm so the
        // NSWindowDelegate's windowWillClose: can publish
        // responseContextId=0 directly (user clicking the X on the
        // plug-in window isn't a request/response IPC event — the
        // engine learns about it via the next response-field poll).
        sirius_gui_set_state (guiState);
       #endif
        const auto serviceGui = [&] {
           #ifdef __APPLE__
            // Drain AppKit events FIRST so a pending close from the
            // previous tick gets picked up before we service any
            // engine-initiated request. Non-blocking; sub-µs when idle.
            sirius_appkit_drain_events();
           #endif
            serviceGuiRequests (gui, plugin);
        };
        const auto serviceState = [&] { serviceStateRequests (stateSrv, plugin); };
        const auto serviceBoth  = [&] { serviceGui(); serviceState(); };
        RingByteStream stream (inRing, outRing, serviceBoth);
        int exitCode = kExitOk;

        while (shouldExit == 0)
        {
            // Service any GUI request that landed since the last buffer.
            // Bounds GUI latency to one audio buffer when audio is
            // flowing; the in-pop onIdle hook covers the idle case.
           #ifdef __APPLE__
            sirius_appkit_drain_events();
           #endif
            serviceGuiRequests (gui, plugin);
            serviceStateRequests (stateSrv, plugin);
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
        plugin->destroy (plugin); // loader destructs at function exit: deinit + dlclose
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
                      "ida_plugin_host: required args missing\n"
                      "  usage: ida_plugin_host --instance-id <id> --mode identity\n"
                      "         ida_plugin_host --instance-id <id> --mode clap --plugin-path <bundle>\n");
        return kExitBadArgs;
    }

   #ifdef __APPLE__
    // M7 S9 — bring AppKit online so the CLAP plug-in's editor (when
    // shown) can create + dispatch into its own top-level NSWindow.
    // Activation policy = Accessory: no Dock icon, no Cmd-Tab entry, but
    // windows can still receive key focus + mouse / keyboard events.
    // Idempotent + main-thread safe. Identity-mode children never touch
    // AppKit but the init is cheap enough to run unconditionally; keeps
    // both modes on a single code path.
    if (mode == "clap")
        sirius_appkit_init();
   #endif

    // SIGTERM / SIGINT → request shutdown. SIGPIPE → ignore (legacy from
    // the S1 pipe transport; harmless to keep).
    std::signal (SIGTERM, onTerminationSignal);
    std::signal (SIGINT,  onTerminationSignal);
    std::signal (SIGPIPE, SIG_IGN);

    // Attach to the rings the engine created before forking us.
    std::unique_ptr<ida::SharedMemoryRegion> e2hRegion;
    std::unique_ptr<ida::SharedMemoryRegion> h2eRegion;
    std::unique_ptr<ida::SharedMemoryRegion> guiRegion;
    std::unique_ptr<IpcQueue> e2hQueue;
    std::unique_ptr<IpcQueue> h2eQueue;
    ida::PluginGuiState*   guiState = nullptr;
    try
    {
        e2hRegion = std::make_unique<ida::SharedMemoryRegion> (
            ida::makeEngineToHostRingName (instanceId), 0,
            ida::SharedMemoryRegion::Mode::OpenExisting);
        h2eRegion = std::make_unique<ida::SharedMemoryRegion> (
            ida::makeHostToEngineRingName (instanceId), 0,
            ida::SharedMemoryRegion::Mode::OpenExisting);
        e2hQueue = std::make_unique<IpcQueue> (
            IpcQueue::attach (e2hRegion->data(), ida::kPluginIpcRingCapacity));
        h2eQueue = std::make_unique<IpcQueue> (
            IpcQueue::attach (h2eRegion->data(), ida::kPluginIpcRingCapacity));

        // M7 S5 — open the GUI state region. The engine created it
        // alongside the audio rings before fork; this is OpenExisting.
        guiRegion = std::make_unique<ida::SharedMemoryRegion> (
            ida::makeGuiStateRegionName (instanceId), 0,
            ida::SharedMemoryRegion::Mode::OpenExisting);
        guiState = ida::PluginGuiState::view (guiRegion->data());
    }
    catch (const std::exception& e)
    {
        std::fprintf (stderr, "ida_plugin_host: shm attach failed: %s\n", e.what());
        return kExitShmErr;
    }

    // M8 S2 — open the plug-in-state region. The engine creates it
    // alongside the audio rings before fork (Task 7); engine versions
    // before that don't create it, so tolerate absence — state IPC is
    // opt-in for forward compat. Every current spawn hits this branch.
    std::unique_ptr<ida::SharedMemoryRegion> stateRegion;
    ida::PluginStateState* stateState = nullptr;
    try
    {
        stateRegion = std::make_unique<ida::SharedMemoryRegion> (
            ida::makeStateRegionName (instanceId), 0,
            ida::SharedMemoryRegion::Mode::OpenExisting);
        stateState = ida::PluginStateState::view (stateRegion->data());
    }
    catch (const std::exception&)
    {
        stateRegion.reset();
        stateState = nullptr;
    }

    if (mode == "identity")
        return runIdentityMode (*e2hQueue, *h2eQueue);

    if (mode == "clap")
    {
        if (pluginPath.empty())
        {
            std::fprintf (stderr,
                          "ida_plugin_host: --mode clap requires --plugin-path <bundle>\n");
            return kExitBadArgs;
        }
        return runClapMode (pluginPath, *e2hQueue, *h2eQueue, guiState, stateState);
    }

    std::fprintf (stderr,
                  "ida_plugin_host: unknown --mode '%s' (supported: identity, clap)\n",
                  mode.c_str());
    return kExitUnknownMode;
}
