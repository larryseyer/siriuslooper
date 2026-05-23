// Tests for ClapBundleLoader (M8 S2) — the shared dlopen + entry init +
// factory + descriptor walk extracted from host_process/main.cpp so both
// the CLAP scanner and the CLAP-mode child process can reuse it.
//
// Failure semantics matter: a malformed bundle must NOT crash the
// scanner (which walks 100s of bundles), and a valid bundle must
// produce descriptors equivalent to what the M7 inline loader produced.
#include "ida/ClapBundleLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

#ifndef IDA_SYNTHETIC_CLAP_PATH
    #error "IDA_SYNTHETIC_CLAP_PATH must be defined for ClapBundleLoaderTests"
#endif

#ifndef IDA_HOST_BINARY_PATH
    #error "IDA_HOST_BINARY_PATH must be defined for ClapBundleLoaderTests"
#endif

using ida::ClapBundleLoader;

TEST_CASE ("ClapBundleLoader loads the synthetic CLAP and reports descriptors",
           "[clap-bundle-loader]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_SYNTHETIC_CLAP_PATH, err);
    REQUIRE (loader.valid());
    REQUIRE (err.empty());

    const auto descriptors = loader.descriptors (IDA_SYNTHETIC_CLAP_PATH);
    REQUIRE (descriptors.size() == 1);
    CHECK (descriptors[0].uniqueId == "com.sirius.synthetic.identity");
    CHECK (descriptors[0].version  == "1.0.0");
    CHECK (descriptors[0].name     == "Sirius Synthetic Identity");
}

TEST_CASE ("ClapBundleLoader returns invalid loader for nonexistent path",
           "[clap-bundle-loader]")
{
    std::string err;
    auto loader = ClapBundleLoader::load ("/this/path/does/not/exist.clap", err);
    CHECK_FALSE (loader.valid());
    CHECK_FALSE (err.empty());
}

TEST_CASE ("ClapBundleLoader returns invalid loader for malformed bundle",
           "[clap-bundle-loader]")
{
    // Point at a path that exists but is not a valid CLAP bundle. The
    // host binary itself works as a malformed-from-CLAP-perspective
    // target: it's a real Mach-O without a `clap_entry` symbol.
    // IDA_HOST_BINARY_PATH is the bare ida_plugin_host executable
    // ($<TARGET_FILE:...>), so we point at it directly — there is no
    // surrounding .app bundle for the test target's copy.
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_HOST_BINARY_PATH, err);
    CHECK_FALSE (loader.valid());
    CHECK_FALSE (err.empty());
}

TEST_CASE ("ClapBundleLoader::createPlugin returns nullptr for unknown id",
           "[clap-bundle-loader]")
{
    std::string err;
    auto loader = ClapBundleLoader::load (IDA_SYNTHETIC_CLAP_PATH, err);
    REQUIRE (loader.valid());

    clap_host_t host {};
    host.clap_version    = CLAP_VERSION_INIT;
    host.name            = "test";
    host.vendor          = "test";
    host.url             = "test";
    host.version         = "0";
    host.get_extension   = [](const clap_host_t*, const char*) -> const void* { return nullptr; };
    host.request_restart = [](const clap_host_t*) {};
    host.request_process = [](const clap_host_t*) {};
    host.request_callback= [](const clap_host_t*) {};

    CHECK (loader.createPlugin (host, "com.example.does.not.exist") == nullptr);
}
