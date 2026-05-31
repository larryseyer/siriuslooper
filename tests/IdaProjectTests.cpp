// Tests for ida::IdaProject — the project unit that owns its tapes (blank-slate
// first-run, Slice 2; spec §2.2). Pure / JUCE-free: the creation timestamp is
// INJECTED as a 14-char yyyymmddhhmmss string so every assertion is
// deterministic and no wall clock is ever read inside a test.
#include "ida/IdaProject.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using ida::IdaProject;

TEST_CASE ("IdaProject::create builds folderId = <timestamp>-<sanitized-name>", "[ida-project]")
{
    const auto p = IdaProject::create ("Untitled", "20260530142233");
    CHECK (p.displayName() == "Untitled");
    CHECK (p.createdTimestamp() == "20260530142233");
    CHECK (p.folderId() == "20260530142233-untitled");
}

TEST_CASE ("IdaProject sanitizes spaces and illegal characters to single underscores", "[ida-project]")
{
    CHECK (IdaProject::create ("My Song",        "20260101000000").folderId()
           == "20260101000000-my_song");
    CHECK (IdaProject::create ("Take 1/Final",   "20260101000000").folderId()
           == "20260101000000-take_1_final");
    CHECK (IdaProject::create ("a:b*c?d",        "20260101000000").folderId()
           == "20260101000000-a_b_c_d");
    // runs of illegal characters collapse to ONE underscore
    CHECK (IdaProject::create ("a   b",          "20260101000000").folderId()
           == "20260101000000-a_b");
    CHECK (IdaProject::create ("dots...here",    "20260101000000").folderId()
           == "20260101000000-dots_here");
}

TEST_CASE ("IdaProject trims leading/trailing underscores and lowercases", "[ida-project]")
{
    CHECK (IdaProject::create ("  Padded  ",  "20260101000000").folderId()
           == "20260101000000-padded");
    CHECK (IdaProject::create ("UPPER",       "20260101000000").folderId()
           == "20260101000000-upper");
    CHECK (IdaProject::create ("---x---",     "20260101000000").folderId()
           == "20260101000000-x");
}

TEST_CASE ("IdaProject with an empty or all-illegal name falls back to 'untitled'", "[ida-project]")
{
    CHECK (IdaProject::create ("",        "20260101000000").folderId()
           == "20260101000000-untitled");
    CHECK (IdaProject::create ("///",     "20260101000000").folderId()
           == "20260101000000-untitled");
    CHECK (IdaProject::create ("   ",     "20260101000000").folderId()
           == "20260101000000-untitled");
}

TEST_CASE ("IdaProject rename changes the display name only, never the folder", "[ida-project]")
{
    auto p = IdaProject::create ("Untitled", "20260530142233");
    const auto folderBefore = p.folderId();
    p.setDisplayName ("My Real Song Name");
    CHECK (p.displayName() == "My Real Song Name");
    CHECK (p.folderId()    == folderBefore);          // folder is immutable
    CHECK (p.folderId()    == "20260530142233-untitled");
}

TEST_CASE ("two same-named projects get distinct folders (timestamp disambiguates)", "[ida-project]")
{
    const auto a = IdaProject::create ("Untitled", "20260530142233");
    const auto b = IdaProject::create ("Untitled", "20260530142259");
    CHECK (a.folderId() != b.folderId());
    CHECK (a.folderId() == "20260530142233-untitled");
    CHECK (b.folderId() == "20260530142259-untitled");
}
