#include "ChannelDetailPanWidTab.h"
#include "../OTTOColours.h"

namespace otto::ui {

ChannelDetailPanWidTab::ChannelDetailPanWidTab()
    : channelColor_(Colours::accent) {

    // Pan knob
    panKnob_ = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag,
                                               juce::Slider::NoTextBox);
    panKnob_->setRange(-1.0, 1.0, 0.01);
    panKnob_->setValue(0.0);
    panKnob_->setDoubleClickReturnValue(true, 0.0);
    panKnob_->onValueChange = [this] { notifyPanChanged(); };
    addAndMakeVisible(*panKnob_);

    panLabel_ = std::make_unique<juce::Label>("", "PAN");
    panLabel_->setJustificationType(juce::Justification::centred);
    panLabel_->setColour(juce::Label::textColourId, Colours::textSecondary);
    panLabel_->setFont(juce::Font(13.0f).boldened());
    addAndMakeVisible(*panLabel_);

    // Width knob
    widthKnob_ = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag,
                                                  juce::Slider::NoTextBox);
    widthKnob_->setRange(0.0, 2.0, 0.01);
    widthKnob_->setValue(1.0);
    widthKnob_->setDoubleClickReturnValue(true, 1.0);
    widthKnob_->onValueChange = [this] { notifyWidthChanged(); };
    addAndMakeVisible(*widthKnob_);

    widthLabel_ = std::make_unique<juce::Label>("", "WIDTH");
    widthLabel_->setJustificationType(juce::Justification::centred);
    widthLabel_->setColour(juce::Label::textColourId, Colours::textSecondary);
    widthLabel_->setFont(juce::Font(13.0f).boldened());
    addAndMakeVisible(*widthLabel_);
}

ChannelDetailPanWidTab::~ChannelDetailPanWidTab() = default;

void ChannelDetailPanWidTab::setChannel(int channelIndex, ChannelType type) {
    channelIndex_ = channelIndex;
    channelType_ = type;
    resized();
}

void ChannelDetailPanWidTab::setChannelColor(juce::Colour color) {
    channelColor_ = color;
    if (panKnob_)
        panKnob_->getProperties().set("fillColor", color.toString());
    if (widthKnob_)
        widthKnob_->getProperties().set("fillColor", color.toString());
}

void ChannelDetailPanWidTab::setPan(float pan) {
    if (panKnob_)
        panKnob_->setValue(pan, juce::dontSendNotification);
}

void ChannelDetailPanWidTab::setWidth(float width) {
    if (widthKnob_)
        widthKnob_->setValue(width, juce::dontSendNotification);
}

float ChannelDetailPanWidTab::getPan() const {
    return panKnob_ ? static_cast<float>(panKnob_->getValue()) : 0.0f;
}

float ChannelDetailPanWidTab::getWidth() const {
    return widthKnob_ ? static_cast<float>(widthKnob_->getValue()) : 1.0f;
}

void ChannelDetailPanWidTab::addListener(ChannelDetailPanWidTabListener* listener) {
    listeners_.add(listener);
}

void ChannelDetailPanWidTab::removeListener(ChannelDetailPanWidTabListener* listener) {
    listeners_.remove(listener);
}

void ChannelDetailPanWidTab::paint(juce::Graphics& g) {
    juce::ignoreUnused(g);
}

void ChannelDetailPanWidTab::resized() {
    auto bounds = getLocalBounds();

    int availH = bounds.getHeight() - kLabelHeight;
    int availW = (bounds.getWidth() - kKnobGap) / 2;
    int knobSize = juce::jmin(kMaxKnobSize, juce::jmax(kMinKnobSize, juce::jmin(availH, availW)));

    int totalW = knobSize * 2 + kKnobGap;
    int startX = (bounds.getWidth() - totalW) / 2;
    int centerY = (bounds.getHeight() - knobSize - kLabelHeight) / 2;

    // PAN knob
    panKnob_->setBounds(startX, centerY, knobSize, knobSize);
    panLabel_->setBounds(startX, centerY + knobSize, knobSize, kLabelHeight);

    // WIDTH knob
    int knob2X = startX + knobSize + kKnobGap;
    widthKnob_->setBounds(knob2X, centerY, knobSize, knobSize);
    widthLabel_->setBounds(knob2X, centerY + knobSize, knobSize, kLabelHeight);
}

void ChannelDetailPanWidTab::notifyPanChanged() {
    float pan = getPan();
    listeners_.call([this, pan](ChannelDetailPanWidTabListener& l) {
        l.panWidTabPanChanged(channelIndex_, channelType_, pan);
    });
}

void ChannelDetailPanWidTab::notifyWidthChanged() {
    float width = getWidth();
    listeners_.call([this, width](ChannelDetailPanWidTabListener& l) {
        l.panWidTabWidthChanged(channelIndex_, channelType_, width);
    });
}

}  // namespace otto::ui
