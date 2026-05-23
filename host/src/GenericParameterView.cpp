#include "ida/GenericParameterView.h"

namespace sirius
{

/// One row in the parameter view: name label on the left, slider on the right.
/// The row owns a parameter listener so external value changes (automation
/// playback, parameter learn, plugin-internal recalculation) flow back to the
/// slider without going through the slider's own user-driven path.
class GenericParameterView::Row final : public juce::Component,
                                        public juce::AudioProcessorParameter::Listener,
                                        private juce::AsyncUpdater
{
public:
    explicit Row (juce::AudioProcessorParameter& parameter)
        : parameter_ (parameter)
    {
        label_.setText (parameter.getName (40), juce::dontSendNotification);
        label_.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label_);

        slider_.setSliderStyle (juce::Slider::LinearHorizontal);
        slider_.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 22);
        slider_.setRange (0.0, 1.0);
        slider_.setValue (parameter.getValue(), juce::dontSendNotification);
        slider_.textFromValueFunction = [&parameter] (double v)
        {
            return parameter.getText (static_cast<float> (v), 40);
        };
        slider_.onValueChange = [this]
        {
            if (suppressOutgoing_) return;
            parameter_.setValueNotifyingHost (static_cast<float> (slider_.getValue()));
        };
        addAndMakeVisible (slider_);

        parameter_.addListener (this);
    }

    ~Row() override
    {
        parameter_.removeListener (this);
        cancelPendingUpdate();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label_.setBounds (area.removeFromLeft (juce::jmax (120, area.getWidth() / 3)));
        slider_.setBounds (area.reduced (4, 0));
    }

    void parameterValueChanged (int /*parameterIndex*/, float /*newValue*/) override
    {
        // May arrive on the audio thread — defer the slider update to the
        // message thread, where touching juce::Slider is safe.
        triggerAsyncUpdate();
    }

    void parameterGestureChanged (int, bool) override {}

private:
    void handleAsyncUpdate() override
    {
        const juce::ScopedValueSetter<bool> guard (suppressOutgoing_, true);
        slider_.setValue (parameter_.getValue(), juce::dontSendNotification);
    }

    juce::AudioProcessorParameter& parameter_;
    juce::Label label_;
    juce::Slider slider_;
    bool suppressOutgoing_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Row)
};

GenericParameterView::GenericParameterView (juce::AudioProcessor& processor)
{
    rows_.reserve (static_cast<std::size_t> (processor.getParameters().size()));
    for (auto* param : processor.getParameters())
    {
        if (param == nullptr || ! param->isAutomatable())
            continue;
        rows_.push_back (std::make_unique<Row> (*param));
        addAndMakeVisible (*rows_.back());
    }

    setSize (480, juce::jmax (rowHeight, totalHeight()));
}

GenericParameterView::~GenericParameterView() = default;

int GenericParameterView::totalHeight() const
{
    return static_cast<int> (rows_.size()) * rowHeight;
}

void GenericParameterView::resized()
{
    auto area = getLocalBounds();
    for (auto& row : rows_)
        row->setBounds (area.removeFromTop (rowHeight));
}

} // namespace sirius
