#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace juce { class AudioProcessorEditor; }

namespace ida
{

class OttoHost;

/// Top-level IDA tab that hosts OTTO's full operator UI (the
/// `juce::AudioProcessorEditor` returned by `OTTOProcessor::createEditor()`,
/// accessed indirectly via `OttoHost::getProcessor()`).
///
/// Lifetime: constructed in MainComponent's ctor after the `OttoHost` member
/// is ready, owned by the tab container. MUST destruct BEFORE the `OttoHost`
/// — the contained editor holds a back-pointer to the processor. The member
/// declaration order in `MainComponent.h` enforces this (ottoHost_ declared
/// before ottoPane_ → reverse-declaration destruction destroys ottoPane_
/// first).
///
/// Hidden affordances: OTTO's unified TransportBar (logo + transport widgets
/// + meter + spectrum) and the OutputRouting section in Preferences are
/// suppressed when `OttoHost` has flagged the processor as embedded — see
/// `OttoHost::Impl::Impl()` and `OTTOProcessor::setEmbeddedInHost(true)`.
class OttoPane final : public juce::Component
{
public:
    explicit OttoPane (OttoHost& host);
    ~OttoPane() override;

    OttoPane (const OttoPane&)            = delete;
    OttoPane& operator= (const OttoPane&) = delete;
    OttoPane (OttoPane&&)                 = delete;
    OttoPane& operator= (OttoPane&&)      = delete;

    void resized() override;
    void paint (juce::Graphics&) override;

    /// Test-only: returns the hosted editor pointer (or nullptr if absent).
    /// Production code should not depend on this — it exists so the headless
    /// `OttoPaneTests` can assert the editor is constructed.
    juce::AudioProcessorEditor* getEditorForTesting() const noexcept { return editor_.get(); }

private:
    std::unique_ptr<juce::AudioProcessorEditor> editor_;
};

} // namespace ida
