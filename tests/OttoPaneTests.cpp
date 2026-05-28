// Headless construction test for ida::OttoPane (S2). Cannot exercise the
// hosted editor's interactive behavior — that is operator-verified per
// CLAUDE.md — but it pins the construction contract: pane builds, hosts
// a non-null editor, and the editor becomes a child Component.

#include "OttoPane.h"
#include "ida/OttoHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("OttoPane constructs with a non-null editor child",
          "[otto-pane-construction]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    ida::OttoPane pane (host);

    auto* editor = pane.getEditorForTesting();
    REQUIRE(editor != nullptr);

    // The editor was added as a child of the pane.
    bool foundEditorChild = false;
    for (int i = 0; i < pane.getNumChildComponents(); ++i)
    {
        if (pane.getChildComponent(i) == editor)
        {
            foundEditorChild = true;
            break;
        }
    }
    REQUIRE(foundEditorChild);
}

TEST_CASE("OttoPane::resized sizes the editor to the pane bounds",
          "[otto-pane-construction]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::OttoHost host;
    ida::OttoPane pane (host);

    pane.setSize (800, 600);

    auto* editor = pane.getEditorForTesting();
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth()  == 800);
    CHECK(editor->getHeight() == 600);
}
