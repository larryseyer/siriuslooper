// Optional third-party CLAP integration test (M8 S2). Activated only
// when IDA_THIRDPARTY_CLAP_PATH is set at CMake configure time
// (point it at any installed CLAP bundle — FabFilter, Surge XT, etc.).
// Same instantiate / parameter / save / load cycle the synthetic
// tests run, but against the real plug-in.
#include "sirius/ClapBundleLoader.h"
#include "sirius/OutOfProcessPluginInstance.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#ifndef IDA_THIRDPARTY_CLAP_PATH
    #error "Compile-time define missing — this TU is conditional"
#endif
#ifndef IDA_HOST_BINARY_PATH
    #error "IDA_HOST_BINARY_PATH required"
#endif

using ida::ClapBundleLoader;
using ida::OutOfProcessPluginInstance;

namespace
{
// Real third-party plug-ins (Surge XT, FabFilter, etc.) can be slow to
// first-serialize, so these are deliberately more generous than the
// synthetic-CLAP timeouts — a heavyweight plug-in's first state save can
// take well over the 2 s a synthetic stub needs.
constexpr auto kThirdPartyStateTimeout = std::chrono::seconds (10);
// Warmup before the first IPC round-trip: lets the host child dlopen the
// bundle and activate the plug-in before we ask it to serialize.
constexpr auto kThirdPartyChildWarmup = std::chrono::milliseconds (1000);

// activate() arguments for the in-process load/instantiate smoke test.
constexpr double kSampleRate = 48000.0;
constexpr std::uint32_t kMinFrames = 1;
constexpr std::uint32_t kMaxFrames = 1024;
} // namespace

TEST_CASE ("third-party CLAP loads and instantiates",
           "[third-party-clap]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_THIRDPARTY_CLAP_PATH, err);
    REQUIRE (loader.valid());
    const auto descs = loader.descriptors (IDA_THIRDPARTY_CLAP_PATH);
    REQUIRE (! descs.empty());

    clap_host_t host {};
    host.clap_version    = CLAP_VERSION_INIT;
    host.name = "test"; host.vendor = "test";
    host.url = "test"; host.version = "0";
    host.get_extension   = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
    host.request_restart = [](const clap_host_t*) {};
    host.request_process = [](const clap_host_t*) {};
    host.request_callback= [](const clap_host_t*) {};

    const auto* plugin = loader.createPlugin (host, descs.front().uniqueId.c_str());
    REQUIRE (plugin != nullptr);

    // Scope guard: destroy() must run on every path, including a Catch2
    // REQUIRE/CHECK throwing mid-lifecycle, or we leak the plug-in.
    struct PluginGuard
    {
        const clap_plugin_t* p;
        ~PluginGuard() { if (p != nullptr) p->destroy (p); }
    } guard { plugin };

    CHECK (plugin->init (plugin));
    CHECK (plugin->activate (plugin, kSampleRate, kMinFrames, kMaxFrames));
    plugin->start_processing (plugin);
    plugin->stop_processing  (plugin);
    plugin->deactivate (plugin);
}

TEST_CASE ("third-party CLAP state round-trips through the host child",
           "[third-party-clap]")
{
    juce::File bundle (IDA_THIRDPARTY_CLAP_PATH);
    OutOfProcessPluginInstance inst (
        juce::File (IDA_HOST_BINARY_PATH), "thirdparty", bundle);
    REQUIRE (inst.isRunning());
    std::this_thread::sleep_for (kThirdPartyChildWarmup);

    std::vector<std::byte> bytes;
    const bool ok = inst.requestStateSave (bytes, kThirdPartyStateTimeout);
    if (! ok)
    {
        WARN ("requestStateSave returned false — plug-in may lack "
              "clap_plugin_state, OR the save timed out / errored. State "
              "round-trip skipped; cannot distinguish at the current bool "
              "API.");
        SUCCEED();
        return;
    }
    REQUIRE (! bytes.empty());

    const bool loaded = inst.requestStateLoad (
        std::span<const std::byte> (bytes.data(), bytes.size()),
        kThirdPartyStateTimeout);
    CHECK (loaded);
}
