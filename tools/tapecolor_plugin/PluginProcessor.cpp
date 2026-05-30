#include "PluginProcessor.h"

namespace tcp = lsfx::tapecolor;

TapeColorTestProcessor::TapeColorTestProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", tcp::createParameterLayout())
{
    // Default the effect ON so the operator can audition immediately — this is
    // a dedicated test plugin, not the always-default-off in-app TAPECOLOR slot.
    if (auto* enabled = apvts_.getParameter (tcp::params::kEnabled))
        enabled->setValueNotifyingHost (1.0f);
}

void TapeColorTestProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp_.prepare (sampleRate, samplesPerBlock, 2);   // stereo-internal DSP
}

bool TapeColorTestProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo in == stereo out only (the DSP is stereo throughout).
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TapeColorTestProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2)
        return;

    // Publish the current parameter state to the DSP via its config-swap.
    dsp_.scratchConfig() = tcp::snapshotFromAPVTS (apvts_);
    dsp_.commitConfig();

    double bpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    dsp_.process (buffer, bpm);
}

void TapeColorTestProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TapeColorTestProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TapeColorTestProcessor();
}
