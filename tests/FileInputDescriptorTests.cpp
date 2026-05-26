#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ida/FileInputDescriptor.h"
#include "ida/InputKind.h"
#include "ida/LoopScope.h"
#include "ida/PlaylistEntryId.h"

TEST_CASE ("LoopScope enum has Off, Track, List", "[file-input][core]")
{
    CHECK (static_cast<int> (ida::LoopScope::Off)   == 0);
    CHECK (static_cast<int> (ida::LoopScope::Track) == 1);
    CHECK (static_cast<int> (ida::LoopScope::List)  == 2);
}

TEST_CASE ("PlaylistEntryId is value-type + equality-comparable", "[file-input][core]")
{
    constexpr ida::PlaylistEntryId a { 1 };
    constexpr ida::PlaylistEntryId b { 1 };
    constexpr ida::PlaylistEntryId c { 2 };

    CHECK (a == b);
    CHECK (a != c);
    CHECK (a.value() == 1);
    CHECK (c.value() == 2);
}

TEST_CASE ("InputKind::FileInput exists and maps to SignalType::Audio",
           "[file-input][core]")
{
    CHECK (ida::signalTypeOf (ida::InputKind::FileInput) == ida::SignalType::Audio);
}

TEST_CASE ("FileInputDescriptor defaults: empty entries, loopScope=Off, windowOpacity=0.92",
           "[file-input][core]")
{
    ida::FileInputDescriptor desc;

    CHECK (desc.entries.empty());
    CHECK (desc.loopScope == ida::LoopScope::Off);
    CHECK (desc.windowOpacity == Catch::Approx (0.92f));
}

TEST_CASE ("FileInputEntry holds entryId + path; missing defaults false",
           "[file-input][core]")
{
    ida::FileInputEntry entry { ida::PlaylistEntryId { 7 }, "/abs/track.wav", {}, false };

    CHECK (entry.entryId == ida::PlaylistEntryId { 7 });
    CHECK (entry.path    == "/abs/track.wav");
    CHECK_FALSE (entry.missing);
    CHECK_FALSE (entry.durationFrames.has_value());
}
