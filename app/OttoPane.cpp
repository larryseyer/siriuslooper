#include "OttoPane.h"

#include "ida/OttoHost.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ida
{

OttoPane::OttoPane (OttoHost& host)
{
    // OttoHost::Impl::Impl() has already called
    // OTTOProcessor::setEmbeddedInHost(true), so the editor's isPluginMode_
    // derivation picks up the embedded path during construction below.
    editor_.reset (host.getProcessor().createEditor());

    if (editor_ != nullptr)
        addAndMakeVisible (*editor_);
}

OttoPane::~OttoPane() = default;

void OttoPane::resized()
{
    if (editor_ == nullptr)
        return;

    // OTTOEditor's desktop default is 1200×1200, min 600×600, resizable.
    // The pane stretches the editor to fill the tab area — OTTOEditor's
    // own ResizableCornerComponent / size-limits constraints in the
    // standalone window do not apply when it is a child Component, so a
    // straight setBounds is safe (and matches how AUv3 hosts embed it).
    editor_->setBounds (getLocalBounds());
}

void OttoPane::paint (juce::Graphics& g)
{
    // The editor paints the entire surface when present. The fallback
    // background appears only if createEditor returned nullptr — a state
    // §4.2 of the integration spec documents as "OTTO UI failed to
    // initialize" — surfacing the empty tab is the operator's signal to
    // file a bug.
    if (editor_ == nullptr)
    {
        g.fillAll (juce::Colours::black);
        g.setColour (juce::Colours::red);
        g.setFont (juce::Font (juce::FontOptions (16.0f)));
        g.drawText ("OTTO UI failed to initialize",
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace ida
