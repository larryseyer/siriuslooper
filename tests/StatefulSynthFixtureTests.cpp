// Tests that drive the StatefulSynthFixture (M8 S2) directly through
// the bundle loader — exercises params + state round-trip in-process.
#include "ida/ClapBundleLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef IDA_STATEFUL_SYNTH_CLAP_PATH
    #error "IDA_STATEFUL_SYNTH_CLAP_PATH must be defined"
#endif

using ida::ClapBundleLoader;

namespace
{
    clap_host_t makeHost()
    {
        clap_host_t h {};
        h.clap_version    = CLAP_VERSION_INIT;
        h.name            = "test"; h.vendor = "test";
        h.url             = "test"; h.version = "0";
        h.get_extension   = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
        h.request_restart = [](const clap_host_t*) {};
        h.request_process = [](const clap_host_t*) {};
        h.request_callback= [](const clap_host_t*) {};
        return h;
    }
}

TEST_CASE ("StatefulSynthFixture exposes 2 parameters with documented metadata",
           "[stateful-synth-fixture]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_STATEFUL_SYNTH_CLAP_PATH, err);
    REQUIRE (loader.valid());

    auto host = makeHost();
    const auto descs = loader.descriptors (IDA_STATEFUL_SYNTH_CLAP_PATH);
    REQUIRE (descs.size() == 1);
    const auto* plugin = loader.createPlugin (host, descs.front().uniqueId.c_str());
    REQUIRE (plugin != nullptr);
    REQUIRE (plugin->init (plugin));

    const auto* params = static_cast<const clap_plugin_params_t*> (
        plugin->get_extension (plugin, CLAP_EXT_PARAMS));
    REQUIRE (params != nullptr);
    CHECK (params->count (plugin) == 2);

    clap_param_info_t info {};
    REQUIRE (params->get_info (plugin, 0, &info));
    CHECK (std::string (info.name) == "Cutoff");
    REQUIRE (params->get_info (plugin, 1, &info));
    CHECK (std::string (info.name) == "Resonance");

    plugin->destroy (plugin);
}

TEST_CASE ("StatefulSynthFixture state.save produces 16 bytes with documented layout",
           "[stateful-synth-fixture]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_STATEFUL_SYNTH_CLAP_PATH, err);
    REQUIRE (loader.valid());
    auto host = makeHost();
    const auto descs = loader.descriptors (IDA_STATEFUL_SYNTH_CLAP_PATH);
    const auto* plugin = loader.createPlugin (host, descs.front().uniqueId.c_str());
    REQUIRE (plugin->init (plugin));

    const auto* state = static_cast<const clap_plugin_state_t*> (
        plugin->get_extension (plugin, CLAP_EXT_STATE));
    REQUIRE (state != nullptr);

    std::vector<std::uint8_t> buf;
    buf.reserve (32);
    struct Ctx { std::vector<std::uint8_t>* dst; } ctx { &buf };
    clap_ostream_t stream {
        &ctx,
        [](const clap_ostream_t* s, const void* d, std::uint64_t n) -> std::int64_t {
            auto* c = static_cast<Ctx*> (s->ctx);
            const auto* p = static_cast<const std::uint8_t*> (d);
            c->dst->insert (c->dst->end(), p, p + n);
            return static_cast<std::int64_t> (n);
        }
    };
    REQUIRE (state->save (plugin, &stream));
    REQUIRE (buf.size() == 16);
    CHECK (std::string (buf.begin(), buf.begin() + 4) == "SLST");

    plugin->destroy (plugin);
}

TEST_CASE ("StatefulSynthFixture state.load rejects mismatched magic",
           "[stateful-synth-fixture]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_STATEFUL_SYNTH_CLAP_PATH, err);
    REQUIRE (loader.valid());
    auto host = makeHost();
    const auto descs = loader.descriptors (IDA_STATEFUL_SYNTH_CLAP_PATH);
    const auto* plugin = loader.createPlugin (host, descs.front().uniqueId.c_str());
    REQUIRE (plugin->init (plugin));

    const auto* state = static_cast<const clap_plugin_state_t*> (
        plugin->get_extension (plugin, CLAP_EXT_STATE));
    REQUIRE (state != nullptr);

    const std::uint8_t bad[16] = { 'B','A','D','M', 1,0,0,0, 0,0,0,0, 0,0,0,0 };
    struct Ctx { const std::uint8_t* data; std::size_t pos; std::size_t cap; }
        ctx { bad, 0, sizeof (bad) };
    clap_istream_t stream {
        &ctx,
        [](const clap_istream_t* s, void* d, std::uint64_t n) -> std::int64_t {
            auto* c = static_cast<Ctx*> (s->ctx);
            const auto take = std::min<std::size_t> (
                static_cast<std::size_t> (n), c->cap - c->pos);
            std::memcpy (d, c->data + c->pos, take);
            c->pos += take;
            return static_cast<std::int64_t> (take);
        }
    };
    CHECK_FALSE (state->load (plugin, &stream));

    plugin->destroy (plugin);
}
