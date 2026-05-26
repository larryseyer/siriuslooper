#include "ida/FileInputSource.h"

namespace ida
{

FileInputSource::FileInputSource (double deviceSampleRate)
    : deviceSampleRate_ (deviceSampleRate)
{
    formatManager_.registerBasicFormats();   // WAV + AIFF
    formatManager_.registerFormat (new juce::FlacAudioFormat(), false);
}

FileInputSource::~FileInputSource() = default;

bool FileInputSource::openReader (const std::string& path)
{
    juce::File file { juce::String (path) };
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader {
        formatManager_.createReaderFor (file) };
    if (reader == nullptr)
        return false;

    currentReader_ = std::move (reader);
    return true;
}

void FileInputSource::closeReader()
{
    currentReader_.reset();
}

int FileInputSource::currentReaderDurationFrames() const noexcept
{
    return currentReader_ != nullptr
         ? static_cast<int> (currentReader_->lengthInSamples) : 0;
}

double FileInputSource::currentReaderSampleRate() const noexcept
{
    return currentReader_ != nullptr ? currentReader_->sampleRate : 0.0;
}

int FileInputSource::currentReaderNumChannels() const noexcept
{
    return currentReader_ != nullptr
         ? static_cast<int> (currentReader_->numChannels) : 0;
}

} // namespace ida
