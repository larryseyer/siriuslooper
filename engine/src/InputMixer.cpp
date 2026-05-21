#include "sirius/InputMixer.h"

#include "sirius/ChannelStrip.h"
#include "sirius/NotificationBus.h"
#include "sirius/OverloadProtection.h"
#include "sirius/TapeStore.h"
#include "sirius/TapeWriter.h"

#include <juce_core/juce_core.h>

#include <cassert>
#include <cstring>

namespace
{
    // Per-instance scratch ceiling for `InputMixer::processBuffer`'s
    // byte→float→byte round-trip. 8192 floats = 16 kHz × 0.5 s — well above
    // any realistic device buffer size (typical: 64..2048 samples). File-
    // scope in the .cpp rather than the header so it does not leak into
    // the engine's public surface, matching the same convention
    // AudioCallback.cpp uses for `kMaxScratchChannels`.
    constexpr std::size_t kMaxScratchSamples = 8192;
}

namespace sirius
{

InputMixer::InputMixer()
    : processingScratch_ (kMaxScratchSamples, 0.0f),
      scratchLeft_ (kMaxScratchSamples, 0.0f),
      scratchRight_ (kMaxScratchSamples, 0.0f)
{
    buses_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    busNodeIds_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    setBusMainOutToHardwareOutput (addFxReturn ("RVB"));
    setBusMainOutToHardwareOutput (addFxReturn ("DLY"));
}

InputMixer::~InputMixer() = default;

void InputMixer::setTapeWriter (TapeWriter* writer) noexcept       { tapeWriter_ = writer; }
void InputMixer::setOverloadProtection (OverloadProtection* o) noexcept { overload_ = o; }
void InputMixer::setTapeStore (sirius::persistence::TapeStore* store) noexcept { tapeStore_ = store; }
void InputMixer::setNotificationBus (NotificationBus* bus) noexcept { notificationBus_ = bus; }

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
    channelNodeIds_.emplace (id.value(), graph_.addNode (MixerNodeKind::Channel));
    return id;
}

void InputMixer::removeChannel (ChannelId id)
{
    channels_.erase (id.value());
    channelSources_.erase (id.value());
    if (auto it = channelNodeIds_.find (id.value()); it != channelNodeIds_.end())
    {
        graph_.removeNode (it->second);
        channelNodeIds_.erase (it);
    }
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

void InputMixer::setChannelInputSource (ChannelId id, int leftDeviceChannel,
                                        int rightDeviceChannel, bool stereo) noexcept
{
    channelSources_.insert_or_assign (
        id.value(), ChannelInputSource { leftDeviceChannel, rightDeviceChannel, stereo });
}

BusId InputMixer::addBus (BusConfig config)
{
    if (buses_.size() >= static_cast<std::size_t> (kMaxInputBuses))
    {
        jassertfalse; // fail loud — losing a routing node silently corrupts the graph
        return BusId { 0 };
    }
    const BusId id { nextBusId_++ };
    buses_.emplace_back (id, config);
    const auto kind = (config.kind == BusKind::FxReturn) ? MixerNodeKind::FxReturn
                                                         : MixerNodeKind::Bus;
    busNodeIds_.push_back (graph_.addNode (kind)); // defaults main-out to primary (Tape)
    return id;
}

BusId InputMixer::addFxReturn (const std::string& name)
{
    return addBus (BusConfig { 2, name, BusKind::FxReturn });
}

int InputMixer::busCount() const noexcept { return static_cast<int> (buses_.size()); }

BusId InputMixer::busIdAt (int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (buses_.size())) return BusId { 0 };
    return buses_[static_cast<std::size_t> (index)].id();
}

BusKind InputMixer::busKindAt (int index) const noexcept
{
    if (index < 0 || index >= static_cast<int> (buses_.size())) return BusKind::Bus;
    return buses_[static_cast<std::size_t> (index)].config().kind;
}

MixerNodeId InputMixer::nodeForBus (BusId id) const noexcept
{
    for (std::size_t i = 0; i < buses_.size(); ++i)
        if (buses_[i].id() == id) return busNodeIds_[i];
    return MixerNodeId {};
}

bool InputMixer::busMainOutIsTape (BusId id) const noexcept
{
    const MixerNodeId node = nodeForBus (id);
    return node.isValid()
        && graph_.mainOutOf (node) == graph_.terminalNode (MixerTerminal::Tape);
}

bool InputMixer::channelIsRegisteredInGraph (ChannelId id) const noexcept
{
    return channelNodeIds_.find (id.value()) != channelNodeIds_.end();
}

void InputMixer::processDeviceInputs (const float* const* deviceIn,
                                      int numDeviceChannels, int numSamples) noexcept
{
    if (deviceIn == nullptr || numDeviceChannels <= 0 || numSamples <= 0) return;
    if (numSamples > static_cast<int> (kMaxScratchSamples))
        numSamples = static_cast<int> (kMaxScratchSamples);

    for (const auto& [channelValue, source] : channelSources_)
    {
        auto chIt = channels_.find (channelValue);
        if (chIt == channels_.end()) continue;

        const auto& channel = chIt->second;
        if (channel.signalType != SignalType::Audio || channel.processing == nullptr)
            continue;

        const int leftCh  = source.left;
        const int rightCh = source.stereo ? source.right : source.left;
        if (leftCh  < 0 || leftCh  >= numDeviceChannels) continue;
        if (rightCh < 0 || rightCh >= numDeviceChannels) continue;
        if (deviceIn[leftCh] == nullptr || deviceIn[rightCh] == nullptr) continue;

        // Gather the source channel(s) into the stereo scratch — a mono source
        // (leftCh == rightCh) lands identically on both rows (dual-mono), so
        // the strip's equal-power pan then positions it in the stereo field.
        const auto byteCount = static_cast<std::size_t> (numSamples) * sizeof (float);
        std::memcpy (scratchLeft_.data(),  deviceIn[leftCh],  byteCount);
        std::memcpy (scratchRight_.data(), deviceIn[rightCh], byteCount);

        float* stereo[2] { scratchLeft_.data(), scratchRight_.data() };
        auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (channel.processing.get());
        strip->process (stereo, 2, numSamples);
    }
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

    // M5 Session 1: Audio chains do real gain/pan work. Copy the byte stream
    // into the pre-allocated float scratch (the byte stream IS a float stream
    // byte-aligned — AudioCallback passes `reinterpret_cast<const std::byte*>`
    // of the live `float*` buffer), run ChannelStrip<Audio>::process in-place
    // on the scratch, then memcpy the scratch back into the TapeWriteMessage.
    // The source `bytes` pointer is never mutated — DirectLayer's raw routes
    // read the same float pointers from AudioCallback and a write through
    // would break the raw-monitor contract. Non-Audio channels skip the DSP
    // path entirely (their chains are stubs until M9/M12/M13).
    const bool isAudio = (channel.signalType == SignalType::Audio
                          && channel.processing != nullptr);

    // Clamp byteCount to scratch capacity (samples * sizeof(float)). M3's
    // earlier clamp already capped at kMaxTapeWriteMessageBytes = 32768 which
    // matches 8192 floats — but keep this clamp explicit so a future
    // re-sizing of either constant cannot quietly silently overflow.
    constexpr std::size_t kScratchByteCap = kMaxScratchSamples * sizeof (float);
    if (byteCount > kScratchByteCap) byteCount = kScratchByteCap;

    // For Audio channels we cast the byte stream to floats — the byteCount
    // MUST be sample-aligned or the trailing 1-3 bytes would slip into the
    // TapeWriteMessage without being attenuated (caller would see a partial
    // raw tail mixed with processed audio). Floor here so the contract holds
    // regardless of what callers (real or test) hand us.
    if (channel.signalType == SignalType::Audio)
        byteCount = (byteCount / sizeof (float)) * sizeof (float);
    if (byteCount == 0) return;

    const std::byte* outBytes = bytes;
    if (isAudio)
    {
        const std::size_t sampleCount = byteCount / sizeof (float);
        std::memcpy (processingScratch_.data(), bytes, byteCount);

        // ChannelStrip<Audio>::process takes non-interleaved float* const*
        // pointers; the inbound buffer is one channel of audio (one input
        // device channel at a time per AudioCallback's dispatch loop), so
        // numChannels = 1 and pan is ignored. Multi-channel input strips
        // come with the OutputMixer surface (Session 2-3 own that path).
        float* channelData[1] { processingScratch_.data() };
        auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (channel.processing.get());
        strip->process (channelData, 1, static_cast<int> (sampleCount));

        outBytes = reinterpret_cast<const std::byte*> (processingScratch_.data());
    }

    if (channel.tapeMode == TapeMode::NoTape || tapeWriter_ == nullptr)
        return;

    TapeWriteMessage msg;
    msg.id = id;
    msg.lmcTime = Rational (0); // M3 has no per-channel LMC time wiring yet; M4 adds it
    msg.payloadByteCount = byteCount;
    std::memcpy (msg.samples.data(), outBytes, byteCount);

    if (! tapeWriter_->tryEnqueue (msg))
    {
        // M6 Session 2 — post the truthfulness signal BEFORE reporting load so
        // a UI drain seeing the notification can correlate it with the load
        // spike. Both calls are noexcept and allocation-free; either or both
        // collaborators may be unbound (tests exercise the partial-wiring
        // case) and we just no-op the missing leg.
        if (notificationBus_ != nullptr)
            notificationBus_->post (NotificationLevel::Warning,
                                    Category::CpuPressure,
                                    "audio thread missed deadline — tape buffer dropped");
        if (overload_ != nullptr)
            overload_->reportLoad (1.0);
    }
}

MixerNodeId InputMixer::nodeForChannel (ChannelId id) const noexcept
{
    if (auto it = channelNodeIds_.find (id.value()); it != channelNodeIds_.end())
        return it->second;
    return MixerNodeId {};
}

InputMixer::MainOutDest InputMixer::classifyMainOut (MixerNodeId dest) const noexcept
{
    if (dest == graph_.terminalNode (MixerTerminal::Tape))           return MainOutDest::Tape;
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput)) return MainOutDest::HardwareOutput;
    return MainOutDest::Bus;
}

bool InputMixer::setChannelMainOutToBus (ChannelId ch, BusId bus)
{ return graph_.setMainOut (nodeForChannel (ch), nodeForBus (bus)); }
bool InputMixer::setChannelMainOutToHardwareOutput (ChannelId ch)
{ return graph_.setMainOut (nodeForChannel (ch), graph_.terminalNode (MixerTerminal::HardwareOutput)); }
bool InputMixer::setChannelMainOutToTape (ChannelId ch)
{ return graph_.setMainOut (nodeForChannel (ch), graph_.terminalNode (MixerTerminal::Tape)); }
bool InputMixer::setBusMainOutToBus (BusId from, BusId to)
{ return graph_.setMainOut (nodeForBus (from), nodeForBus (to)); }
bool InputMixer::setBusMainOutToHardwareOutput (BusId bus)
{ return graph_.setMainOut (nodeForBus (bus), graph_.terminalNode (MixerTerminal::HardwareOutput)); }
bool InputMixer::setBusMainOutToTape (BusId bus)
{ return graph_.setMainOut (nodeForBus (bus), graph_.terminalNode (MixerTerminal::Tape)); }
InputMixer::MainOutDest InputMixer::channelMainOut (ChannelId ch) const noexcept
{ return classifyMainOut (graph_.mainOutOf (nodeForChannel (ch))); }
InputMixer::MainOutDest InputMixer::busMainOut (BusId bus) const noexcept
{ return classifyMainOut (graph_.mainOutOf (nodeForBus (bus))); }
bool InputMixer::setChannelSend (ChannelId ch, BusId fxReturn, float level)
{ return graph_.setSend (nodeForChannel (ch), nodeForBus (fxReturn), level); }
bool InputMixer::setBusSend (BusId source, BusId fxReturn, float level)
{ return graph_.setSend (nodeForBus (source), nodeForBus (fxReturn), level); }
float InputMixer::channelSendLevel (ChannelId ch, BusId fxReturn) const noexcept
{ return graph_.sendLevel (nodeForChannel (ch), nodeForBus (fxReturn)); }

ProcessingChain* InputMixer::processingChainFor (ChannelId id) noexcept
{
    auto it = channels_.find (id.value());
    if (it == channels_.end()) return nullptr;
    return it->second.processing.get();
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
