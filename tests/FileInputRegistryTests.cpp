#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ida/FileInputRegistry.h"
#include "ida/FileInputDescriptor.h"

TEST_CASE ("FileInputRegistry::registerFileInput stores the descriptor and returns an InputId",
           "[file-input][registry]")
{
    ida::FileInputRegistry registry { 48000.0 };

    ida::FileInputDescriptor desc;
    desc.displayName = "Setlist";
    const auto id = registry.registerFileInput (desc);

    REQUIRE (id.value() >= 0);
    REQUIRE (registry.fileInputDescriptor (id) != nullptr);
    CHECK (registry.fileInputDescriptor (id)->displayName == "Setlist");
}

TEST_CASE ("FileInputRegistry::addFileInputEntry appends a playlist entry to the descriptor",
           "[file-input][registry]")
{
    ida::FileInputRegistry registry { 48000.0 };
    const auto id = registry.registerFileInput (ida::FileInputDescriptor {});

    const auto eid = registry.addFileInputEntry (id, "/tmp/whatever.wav");
    CHECK (eid.value() >= 0);

    const auto* desc = registry.fileInputDescriptor (id);
    REQUIRE (desc != nullptr);
    REQUIRE (desc->entries.size() == 1u);
    CHECK (desc->entries[0].path == "/tmp/whatever.wav");
}

TEST_CASE ("FileInputRegistry::setFileInputWindowOpacity clamps to [0.5, 1.0]",
           "[file-input][registry]")
{
    ida::FileInputRegistry registry { 48000.0 };
    const auto id = registry.registerFileInput ({});

    registry.setFileInputWindowOpacity (id, 0.3f);
    CHECK (registry.fileInputDescriptor (id)->windowOpacity == Catch::Approx (0.5f));

    registry.setFileInputWindowOpacity (id, 1.5f);
    CHECK (registry.fileInputDescriptor (id)->windowOpacity == Catch::Approx (1.0f));

    registry.setFileInputWindowOpacity (id, 0.8f);
    CHECK (registry.fileInputDescriptor (id)->windowOpacity == Catch::Approx (0.8f));
}

TEST_CASE ("FileInputRegistry::setFileInputAlwaysOnTop flips the descriptor field",
           "[file-input][registry]")
{
    ida::FileInputRegistry registry { 48000.0 };
    const auto id = registry.registerFileInput ({});

    REQUIRE (registry.fileInputDescriptor (id) != nullptr);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == false);

    registry.setFileInputAlwaysOnTop (id, true);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == true);

    registry.setFileInputAlwaysOnTop (id, false);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == false);

    // Unknown id is a no-op (does not crash, does not mutate other entries).
    registry.setFileInputAlwaysOnTop (ida::InputId (999999), true);
    CHECK (registry.fileInputDescriptor (id)->alwaysOnTop == false);
}

TEST_CASE ("FileInputRegistry::resolveFileInputPull returns a valid callable that consumes the source's ring",
           "[file-input][registry][resolve]")
{
    ida::FileInputRegistry registry (48000.0);

    // Register a file input (no entries needed — the resolve path doesn't
    // depend on playlist state). Use a synthetic descriptor.
    ida::FileInputDescriptor desc {};
    desc.displayName = "TestFile";
    const auto id = registry.registerFileInput (desc);

    // Resolve the callable; it must be valid.
    const auto callable = registry.resolveFileInputPull (id);
    REQUIRE (callable.valid());
    REQUIRE (callable.fn != nullptr);
    REQUIRE (callable.userdata != nullptr);

    // Unknown ids resolve to an invalid callable.
    const auto bogus = registry.resolveFileInputPull (ida::InputId { 999999999 });
    REQUIRE_FALSE (bogus.valid());
    REQUIRE (bogus.fn == nullptr);
}
