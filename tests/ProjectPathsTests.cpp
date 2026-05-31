// Tests for ida::persistence project-scoped tape paths (blank-slate Slice 2;
// spec §2.2/§2.1). The app-support root is INJECTED as a temp juce::File so the
// real ~/Library is never touched and assertions are hermetic.
#include "ida/ProjectPaths.h"

#include "ida/IdaProject.h"
#include "ida/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using ida::IdaProject;
using ida::TapeId;
using ida::persistence::projectTapesDir;
using ida::persistence::tapeFileFor;
using ida::persistence::tapeFileName;

TEST_CASE ("tapeFileName maps a 1-based TapeId to tape_<x>.idatape", "[project-paths]")
{
    CHECK (tapeFileName (TapeId (1)) == "tape_1.idatape");
    CHECK (tapeFileName (TapeId (2)) == "tape_2.idatape");
    CHECK (tapeFileName (TapeId (42)) == "tape_42.idatape");
}

TEST_CASE ("projectTapesDir nests the project folder under the app-support root", "[project-paths]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ida-projectpaths-test-root");
    const auto project = IdaProject::create ("Untitled", "20260530142233");

    const auto dir = projectTapesDir (root, project);
    CHECK (dir.getFileName() == "20260530142233-untitled");
    CHECK (dir.getParentDirectory() == root);
}

TEST_CASE ("tapeFileFor resolves <root>/<folderId>/tape_<x>.idatape", "[project-paths]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ida-projectpaths-test-root");
    const auto project = IdaProject::create ("My Song", "20260101000000");

    const auto file = tapeFileFor (root, project, TapeId (3));
    CHECK (file.getFileName() == "tape_3.idatape");
    CHECK (file.getParentDirectory().getFileName() == "20260101000000-my_song");
    // No tape path escapes the project folder (§2.1 no-orphan structural guard).
    CHECK (file.isAChildOf (projectTapesDir (root, project)));
    CHECK (file.isAChildOf (root));
}

TEST_CASE ("two same-named projects resolve to distinct tape paths", "[project-paths]")
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ida-projectpaths-test-root");
    const auto a = IdaProject::create ("Untitled", "20260530142233");
    const auto b = IdaProject::create ("Untitled", "20260530142259");

    CHECK (tapeFileFor (root, a, TapeId (1))
           != tapeFileFor (root, b, TapeId (1)));
}
