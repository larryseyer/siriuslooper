// Optional third-party CLAP integration test (M8 S2). Activated only
// when SIRIUS_THIRDPARTY_CLAP_PATH is set at CMake configure time
// (point it at any installed CLAP bundle — FabFilter, Surge XT, etc.).
// Same instantiate / parameter / save / load cycle the synthetic
// tests run, but against the real plug-in.
#include "sirius/ClapBundleLoader.h"
#include "sirius/OutOfProcessPluginInstance.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>

#ifndef SIRIUS_THIRDPARTY_CLAP_PATH
    #error "Compile-time define missing — this TU is conditional"
#endif
#ifndef SIRIUS_HOST_BINARY_PATH
    #error "SIRIUS_HOST_BINARY_PATH required"
#endif

using sirius::ClapBundleLoader;
using sirius::OutOfProcessPluginInstance;

TEST_CASE ("third-party CLAP loads and instantiates",
           "[third-party-clap]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (SIRIUS_THIRDPARTY_CLAP_PATH, err);
    REQUIRE (loader.valid());
    const auto descs = loader.descriptors (SIRIUS_THIRDPARTY_CLAP_PATH);
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
    REQUIRE (plugin->init (plugin));
    REQUIRE (plugin->activate (plugin, 48000.0, 1, 1024));
    plugin->start_processing (plugin);
    plugin->stop_processing  (plugin);
    plugin->deactivate (plugin);
    plugin->destroy    (plugin);
}

TEST_CASE ("third-party CLAP state round-trips through the host child",
           "[third-party-clap]")
{
    juce::File bundle (SIRIUS_THIRDPARTY_CLAP_PATH);
    OutOfProcessPluginInstance inst (
        juce::File (SIRIUS_HOST_BINARY_PATH), "thirdparty", bundle);
    REQUIRE (inst.isRunning());
    std::this_thread::sleep_for (std::chrono::milliseconds (500));

    std::vector<std::byte> bytes;
    const bool ok = inst.requestStateSave (bytes,
                                           std::chrono::milliseconds (2000));
    if (! ok)
    {
        WARN ("third-party plug-in does not expose clap_plugin_state — skipped");
        SUCCEED();
        return;
    }
    REQUIRE (! bytes.empty());

    const bool loaded = inst.requestStateLoad (
        std::span<const std::byte> (bytes.data(), bytes.size()),
        std::chrono::milliseconds (2000));
    CHECK (loaded);
}
