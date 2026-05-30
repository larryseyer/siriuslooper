// TAPECOLOR A/B — a thin standalone plugin wrapper around the shared
// lsfx::tapecolor::TapeColorProcessor DSP, built so the operator can A/B the
// asinh vs Jiles-Atherton saturation models on real music in a DAW (Reaper).
//
// This is an experiment/test harness, NOT a shipping product. It exposes the
// full TapeColorConfig as host-automatable APVTS parameters via JUCE's generic
// editor — including "Saturation Model" (Asinh / J-A) and "Enabled". Level and
// frequency response are matched inside the DSP, so toggling Saturation Model
// is an honest character-only comparison.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <lsfx_tapecolor/lsfx_tapecolor.h>

class TapeColorTestProcessor : public juce::AudioProcessor
{
public:
    TapeColorTestProcessor();
    ~TapeColorTestProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor (*this);
    }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TAPECOLOR A/B"; }
    bool   acceptsMidi() const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    juce::AudioProcessorValueTreeState apvts_;
    lsfx::TapeColorProcessor           dsp_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeColorTestProcessor)
};
