// Tests for PluginStateState (M8 S2) — POD layout used by the host
// child and the engine to round-trip plug-in state bytes.
#include "ida/PluginStateRegion.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

using ida::PluginStateState;
using ida::makeStateRegionName;

TEST_CASE ("PluginStateState size fits within the per-instance shm budget",
           "[plugin-state-region]")
{
    // Two 64KiB payload buffers (request + response) ~= 128KiB, plus a
    // handful of atomic counters. The 256KiB ceiling leaves comfortable
    // headroom over that ~128KiB while keeping the per-instance shm
    // footprint predictable; if a future plug-in needs more, that's the
    // chunked-protocol case the spec flags as out-of-scope.
    STATIC_REQUIRE (sizeof (PluginStateState) <= 256u * 1024u);
    STATIC_REQUIRE (std::is_standard_layout_v<PluginStateState>);
}

TEST_CASE ("PluginStateState atomic members are lock-free on this platform",
           "[plugin-state-region]")
{
    PluginStateState s;
    CHECK (s.requestSeq    .is_lock_free());
    CHECK (s.requestKind   .is_lock_free());
    CHECK (s.requestBytes  .is_lock_free());
    CHECK (s.responseSeq   .is_lock_free());
    CHECK (s.responseStatus.is_lock_free());
    CHECK (s.responseBytes .is_lock_free());
}

TEST_CASE ("makeStateRegionName follows the .state suffix convention",
           "[plugin-state-region]")
{
    // Mirrors makeEngineToHostRingName / makeGuiStateRegionName —
    // /sirius.<instanceId>.<suffix>. The host child's
    // SharedMemoryRegion::OpenExisting consumer must match this exactly.
    CHECK (makeStateRegionName ("abc") == "/sirius.abc.state");
    CHECK (makeStateRegionName ("")    == "/sirius..state");
}
