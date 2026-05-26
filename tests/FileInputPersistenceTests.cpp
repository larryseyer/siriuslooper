#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_core/juce_core.h>

#include "ida/FileInputDescriptor.h"
#include "ida/FileInputPersistence.h"
#include "ida/FileInputRegistry.h"
#include "ida/LoopScope.h"

TEST_CASE ("Session JSON round-trips a file input with playlist + loopScope + opacity",
           "[file-input][persistence]")
{
    ida::FileInputRegistry registry { 48000.0 };

    ida::FileInputDescriptor desc;
    desc.displayName   = "Setlist A";
    desc.loopScope     = ida::LoopScope::List;
    desc.windowOpacity = 0.75f;
    const auto id = registry.registerFileInput (desc);
    registry.addFileInputEntry (id, "/abs/a.wav");
    registry.addFileInputEntry (id, "/abs/b.flac");

    const auto json = ida::serializeFileInputs (registry);

    ida::FileInputRegistry registry2 { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry2, json));

    // Look up the round-tripped file input by displayName (InputId values
    // may differ across sessions).
    const ida::FileInputDescriptor* found = nullptr;
    for (const auto& [k, d] : registry2.allFileInputDescriptors())
        if (d.displayName == "Setlist A") { found = &d; break; }

    REQUIRE (found != nullptr);
    CHECK (found->loopScope     == ida::LoopScope::List);
    CHECK (found->windowOpacity == Catch::Approx (0.75f));
    REQUIRE (found->entries.size() == 2u);
    CHECK (found->entries[0].path == "/abs/a.wav");
    CHECK (found->entries[1].path == "/abs/b.flac");
}

TEST_CASE ("JSON without fileInputs array loads clean (backward compat)",
           "[file-input][persistence]")
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    // Other input-mixer state present but no "fileInputs".
    root->setProperty ("buses", juce::Array<juce::var> {});

    ida::FileInputRegistry registry { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry, juce::var (root.get())));
    CHECK (registry.allFileInputDescriptors().empty());
}

TEST_CASE ("Session JSON round-trips alwaysOnTop=true",
           "[file-input][persistence]")
{
    ida::FileInputRegistry registry { 48000.0 };

    ida::FileInputDescriptor desc;
    desc.displayName  = "Pinned";
    desc.alwaysOnTop  = true;
    const auto id = registry.registerFileInput (desc);
    registry.addFileInputEntry (id, "/abs/x.wav");

    const auto json = ida::serializeFileInputs (registry);

    ida::FileInputRegistry registry2 { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry2, json));

    const ida::FileInputDescriptor* found = nullptr;
    for (const auto& [k, d] : registry2.allFileInputDescriptors())
        if (d.displayName == "Pinned") { found = &d; break; }

    REQUIRE (found != nullptr);
    CHECK (found->alwaysOnTop == true);
}

TEST_CASE ("Missing alwaysOnTop key in JSON defaults to false",
           "[file-input][persistence]")
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> fileInputs;
    juce::DynamicObject::Ptr fi = new juce::DynamicObject();
    fi->setProperty ("displayName",   "Legacy");
    fi->setProperty ("loopScope",     "off");
    fi->setProperty ("windowOpacity", 0.92);
    // alwaysOnTop intentionally NOT set — simulates a session JSON saved
    // before this slice landed.
    fi->setProperty ("entries", juce::Array<juce::var> {});
    fileInputs.add (juce::var (fi.get()));
    root->setProperty ("fileInputs", fileInputs);

    ida::FileInputRegistry registry { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry, juce::var (root.get())));

    REQUIRE (registry.allFileInputDescriptors().size() == 1u);
    CHECK (registry.allFileInputDescriptors().begin()->second.alwaysOnTop
           == false);
}

TEST_CASE ("Out-of-range windowOpacity is clamped to [0.5, 1.0] on read",
           "[file-input][persistence]")
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> fileInputs;
    juce::DynamicObject::Ptr fi = new juce::DynamicObject();
    fi->setProperty ("displayName",   "X");
    fi->setProperty ("loopScope",     "off");
    fi->setProperty ("windowOpacity", 0.2);     // below floor
    fi->setProperty ("entries", juce::Array<juce::var> {});
    fileInputs.add (juce::var (fi.get()));
    root->setProperty ("fileInputs", fileInputs);

    ida::FileInputRegistry registry { 48000.0 };
    REQUIRE (ida::deserializeFileInputs (registry, juce::var (root.get())));

    REQUIRE (registry.allFileInputDescriptors().size() == 1u);
    CHECK (registry.allFileInputDescriptors().begin()->second.windowOpacity
           == Catch::Approx (0.5f));
}
