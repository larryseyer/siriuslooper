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
