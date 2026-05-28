// Tests for ida::OttoHost::getProcessor (S2 — engine seam for OttoPane).
// Mirrors the OttoHostRenderTests pattern: ScopedJuceInitialiser_GUI is
// required because OttoHost owns a juce::Timer whose ctor touches the
// MessageManager. These tests pin the accessor's wiring and the embedded-
// in-host flag's runtime effect — the OttoPane integration test (the GUI
// surface itself) is operator-verified per CLAUDE.md.

#include "ida/OttoHost.h"

#include <PluginProcessor.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

using ida::OttoHost;

TEST_CASE("OttoHost::getProcessor returns the embedded OTTOProcessor instance",
          "[otto-host-processor-access]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;

    juce::AudioProcessor& base = host.getProcessor();

    // The accessor must vend an OTTOProcessor dynamically — confirm via
    // dynamic_cast that the runtime type is what S1 embedded.
    auto* otto = dynamic_cast<OTTOProcessor*>(&base);
    REQUIRE(otto != nullptr);
}

TEST_CASE("OttoHost flips OTTOProcessor::isEmbeddedInHost on construction",
          "[otto-host-processor-access]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    auto& otto = dynamic_cast<OTTOProcessor&>(host.getProcessor());

    // S2 contract: OttoHost::Impl::Impl() calls setEmbeddedInHost(true) so
    // OTTOEditor's downstream isPluginMode_ derivation picks up the flag.
    REQUIRE(otto.isEmbeddedInHost());
}

TEST_CASE("OttoHost::getProcessor is pointer-stable across calls",
          "[otto-host-processor-access]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;

    juce::AudioProcessor* first  = &host.getProcessor();
    juce::AudioProcessor* second = &host.getProcessor();

    CHECK(first == second);
}
