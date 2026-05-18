#include "sirius/InputMixer.h"

#include "sirius/OverloadProtection.h"
#include "sirius/TapeStore.h"
#include "sirius/TapeWriter.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <cstring>

namespace sirius
{

InputMixer::InputMixer() = default;
InputMixer::~InputMixer() = default;

void InputMixer::setTapeWriter (TapeWriter* writer) noexcept       { tapeWriter_ = writer; }
void InputMixer::setOverloadProtection (OverloadProtection* o) noexcept { overload_ = o; }
void InputMixer::setTapeStore (sirius::persistence::TapeStore* store) noexcept { tapeStore_ = store; }

void InputMixer::registerInput (InputId id, const InputDescriptor& desc)
{
    InputState state { desc, desc.rawDirectMonitor, desc.enabled, desc.defaults };
    inputs_.insert_or_assign (id.value(), std::move (state));
}

void InputMixer::setInputRawDirect (InputId id, bool enabled)
{
    auto it = inputs_.find (id.value());
    if (it != inputs_.end()) it->second.rawDirectMonitor = enabled;
}

void InputMixer::setInputEnabled (InputId id, bool enabled)
{
    auto it = inputs_.find (id.value());
    if (it != inputs_.end()) it->second.enabled = enabled;
}

void InputMixer::setInputDefaults (InputId id, ChannelDefaults defaults)
{
    auto it = inputs_.find (id.value());
    if (it != inputs_.end()) it->second.defaults = defaults;
}

ChannelId InputMixer::addChannel (InputId source, SignalType type)
{
    const ChannelId id (nextChannelId_++);
    TapeMode mode = TapeMode::NoTape;
    if (auto it = inputs_.find (source.value()); it != inputs_.end())
        mode = it->second.defaults.defaultTapeMode;

    channels_.emplace (id.value(), Channel (id, type, source, mode));
    return id;
}

void InputMixer::removeChannel (ChannelId id)
{
    channels_.erase (id.value());
}

void InputMixer::setChannelTapeMode (ChannelId id, TapeMode mode)
{
    auto it = channels_.find (id.value());
    if (it == channels_.end()) return;

    it->second.tapeMode = mode;

    // For NonDestructive channels, ensure the params partial file exists as
    // soon as the mode is set. Touching here (message thread, set-once) avoids
    // any RT-safety deviation that would result from doing filesystem I/O on the
    // audio thread inside processBuffer. M5's real DSP will append events to this
    // file; for M3 it remains empty (Audio chains are no-op — M3 spec
    // §"What 'dry' means in M3").
    if (mode == TapeMode::NonDestructive && tapeWriter_ != nullptr)
        tapeWriter_->touchParamsPartial (id);
}

void InputMixer::processBuffer (ChannelId id,
                                const std::byte* bytes,
                                std::size_t byteCount) noexcept
{
    if (bytes == nullptr || byteCount == 0) return;
    if (byteCount > kMaxTapeWriteMessageBytes) byteCount = kMaxTapeWriteMessageBytes;

    auto it = channels_.find (id.value());
    if (it == channels_.end()) return;

    const auto& channel = it->second;
    // Processing chain is a no-op in M3; the call exists so the audio-thread
    // shape is right when M5 fills in real DSP.
    (void) channel.processing;

    if (channel.tapeMode == TapeMode::NoTape || tapeWriter_ == nullptr)
        return;

    TapeWriteMessage msg;
    msg.id = id;
    msg.lmcTime = Rational (0); // M3 has no per-channel LMC time wiring yet; M4 adds it
    msg.payloadByteCount = byteCount;
    std::memcpy (msg.samples.data(), bytes, byteCount);

    if (! tapeWriter_->tryEnqueue (msg) && overload_ != nullptr)
        overload_->reportLoad (1.0);
}

void InputMixer::finalizeChannel (ChannelId id)
{
    if (tapeWriter_ == nullptr || tapeStore_ == nullptr) return;

    const std::filesystem::path partial = tapeWriter_->flushChannel (id);
    if (! std::filesystem::exists (partial)) return;

    juce::File partialFile (juce::String (partial.string()));
    juce::MemoryBlock bytes;
    if (! partialFile.loadFileAsData (bytes))
    {
        juce::Logger::writeToLog ("InputMixer::finalizeChannel: cannot read partial: "
                                  + partialFile.getFullPathName());
        return;
    }

    (void) tapeStore_->store (bytes);  // content-addressed hash returned;
                                       // structure-layer mapping (TapeId → hash)
                                       // lands in M11 SAF
    partialFile.deleteFile();
}

} // namespace sirius
