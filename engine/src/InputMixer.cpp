#include "sirius/InputMixer.h"

#include "sirius/OverloadProtection.h"
#include "sirius/TapeWriter.h"

#include <cassert>
#include <cstring>

namespace sirius
{

InputMixer::InputMixer() = default;
InputMixer::~InputMixer() = default;

void InputMixer::setTapeWriter (TapeWriter* writer) noexcept       { tapeWriter_ = writer; }
void InputMixer::setOverloadProtection (OverloadProtection* o) noexcept { overload_ = o; }

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
    if (it != channels_.end()) it->second.tapeMode = mode;
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
    // Session 3 fills in the read-partial → sha256 → TapeStore::store flow.
    // Session 2 provides the entry point so InputMixerTests can compile
    // against the full API surface.
    (void) id;
}

} // namespace sirius
