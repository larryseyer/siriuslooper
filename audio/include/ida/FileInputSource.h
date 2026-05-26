#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <string>

namespace ida
{

/// Per-file-input engine: owns the disk-reader stack and (later) the
/// SPSC ring + worker-thread transport loop. White-paper V9 §6.6 / §7.2.
/// Lives in audio/ (not engine/) because it depends on juce_audio_formats.
class FileInputSource
{
public:
    explicit FileInputSource (double deviceSampleRate);
    ~FileInputSource();

    FileInputSource (const FileInputSource&) = delete;
    FileInputSource& operator= (const FileInputSource&) = delete;

    /// Opens `path` and replaces any previously-open reader. Returns
    /// true on success. Message thread only.
    bool openReader (const std::string& path);

    /// Closes the current reader. Safe to call when no reader is open.
    void closeReader();

    /// Introspection on the currently-open reader. All return 0 / 0.0 / 0
    /// when no reader is open.
    int    currentReaderDurationFrames() const noexcept;
    double currentReaderSampleRate()     const noexcept;
    int    currentReaderNumChannels()    const noexcept;

private:
    double deviceSampleRate_;
    juce::AudioFormatManager formatManager_;
    std::unique_ptr<juce::AudioFormatReader> currentReader_;
};

} // namespace ida
