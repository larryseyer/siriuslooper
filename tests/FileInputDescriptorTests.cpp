#include <catch2/catch_test_macros.hpp>

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
