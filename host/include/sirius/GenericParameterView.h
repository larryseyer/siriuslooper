#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace sirius
{

/// A minimal, plugin-format-agnostic parameter view (white paper Parts XIV /
/// the M5 pull-forward of the M7 preparation view). Given any
/// `juce::AudioProcessor` — a hosted plugin instance, a test processor, an
/// internal effect — it renders one row per parameter: a name label and a
/// normalized-range slider that drives the parameter while the parameter's
/// own value changes flow back to the slider in real time.
///
/// The point of this component is to make hosted effect chains visible at all.
/// Validating a hosted plugin without a way to *see* it is impossible; the
/// generic view is the minimum surface the M5 milestone test (operator-run)
/// needs in order to confirm parameter automation round-trips.
class GenericParameterView final : public juce::Component
{
public:
    explicit GenericParameterView (juce::AudioProcessor& processor);
    ~GenericParameterView() override;

    void resized() override;

    /// The fixed pixel height each parameter row occupies. Exposed so a parent
    /// can size a scroll viewport correctly without recalculating from the
    /// parameter count.
    static constexpr int rowHeight = 28;

    /// The total pixel height needed to show every parameter without a scroll.
    int totalHeight() const;

private:
    class Row;
    std::vector<std::unique_ptr<Row>> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericParameterView)
};

} // namespace sirius
