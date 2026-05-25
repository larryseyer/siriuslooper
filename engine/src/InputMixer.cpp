#include "ida/InputMixer.h"

#include "ida/ChannelStrip.h"
#include "ida/NotificationBus.h"
#include "ida/OutputMixer.h"
#include "ida/OverloadProtection.h"
#include "ida/TapeStore.h"
#include "ida/TapeWriter.h"

#include <juce_core/juce_core.h>

#include <algorithm>
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

namespace ida
{

namespace
{
    // Slice E3 helper — adjust the active-sender count on the bus that owns
    // `node`, by `delta`. No-op when `node` doesn't match any registered
    // bus (terminals, channels, tape nodes).
    void adjustBusCountForNode (MixerNodeId node, int delta,
                                const std::vector<MixerNodeId>& busNodeIds,
                                std::vector<Bus>& buses) noexcept
    {
        for (std::size_t i = 0; i < busNodeIds.size(); ++i)
        {
            if (busNodeIds[i] == node)
            {
                buses[i].adjustActiveSenderCount (delta);
                return;
            }
        }
    }
}

InputMixer::InputMixer()
    : processingScratch_ (kMaxScratchSamples, 0.0f),
      scratchLeft_ (kMaxScratchSamples, 0.0f),
      scratchRight_ (kMaxScratchSamples, 0.0f),
      scratchLeftPre_ (kMaxScratchSamples, 0.0f),
      scratchRightPre_ (kMaxScratchSamples, 0.0f),
      tapeMixLeft_  (static_cast<std::size_t> (kMaxTapes), std::vector<float> (kMaxScratchSamples, 0.0f)),
      tapeMixRight_ (static_cast<std::size_t> (kMaxTapes), std::vector<float> (kMaxScratchSamples, 0.0f)),
      tapeTouched_  (static_cast<std::size_t> (kMaxTapes), 0)
{
    buses_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    busNodeIds_.reserve (static_cast<std::size_t> (kMaxInputBuses));
    tapeTerminals_.reserve (static_cast<std::size_t> (kMaxTapes));
    tapeTerminals_.push_back ({ 1, graph_.terminalNode (MixerTerminal::Tape) });
}

InputMixer::~InputMixer()
{
    // V9 Slice 3 — remove every auto-created MON channel from the attached
    // OutputMixer BEFORE this InputMixer's `postStrip_` storage destructs.
    // OutputMixer's `setChannelAudioSource` holds raw pointers into those
    // buffers; leaving the channel registered would let the next audio
    // callback dereference dangling memory on the project-load path
    // (MainComponent destroys+rebuilds the InputMixer while the OutputMixer
    // survives the swap).
    if (outputMixer_ != nullptr)
        for (auto& [_, state] : channelMonitorRoutes_)
            if (state.outputChannelId.has_value())
                outputMixer_->removeChannel (*state.outputChannelId);
}

void InputMixer::setTapeWriter (TapeWriter* writer) noexcept       { tapeWriter_ = writer; }
void InputMixer::setOverloadProtection (OverloadProtection* o) noexcept { overload_ = o; }
void InputMixer::setTapeStore (ida::persistence::TapeStore* store) noexcept { tapeStore_ = store; }
void InputMixer::setNotificationBus (NotificationBus* bus) noexcept { notificationBus_ = bus; }
void InputMixer::setTapeSink (ITapeSink* sink) noexcept { tapeSink_ = sink; }
void InputMixer::setDirectLayer (DirectLayer* layer) noexcept { directLayer_ = layer; }
void InputMixer::attachOutputMixer (OutputMixer* output) noexcept { outputMixer_ = output; }

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

    // V9 Slice 2 — allocate the per-channel post-strip stereo storage on
    // the message thread (audio thread never touches `postStrip_`'s map
    // structure; it only reads through the pointer handed back by
    // `postStripPointer`). Sized to `kMaxScratchSamples`, the engine-wide
    // scratch ceiling shared with `scratchLeft_/scratchRight_`. Both sides
    // are zeroed; that's the silent-default OutputMixer reads if Slice 3
    // cached the pointer before the first `renderInputGraph` call.
    auto& slot = postStrip_[id.value()];
    slot[0].assign (kMaxScratchSamples, 0.0f);
    slot[1].assign (kMaxScratchSamples, 0.0f);

    return id;
}

void InputMixer::removeChannel (ChannelId id)
{
    // V9 Slice 3: tear down the auto-created MON OutputMixer channel BEFORE
    // this channel's `postStrip_` storage is freed. OutputMixer's
    // `setChannelAudioSource` holds raw pointers into those buffers; reading
    // a dangling pointer on the audio thread would be a use-after-free.
    // Caller is responsible for the audio-callback-detached bracket around
    // removeChannel; here we just ensure the OutputMixer channel comes out
    // first (and only then `postStrip_.erase` later in this function).
    if (auto it = channelMonitorRoutes_.find (id.value()); it != channelMonitorRoutes_.end())
    {
        if (outputMixer_ != nullptr && it->second.outputChannelId.has_value())
            outputMixer_->removeChannel (*it->second.outputChannelId);
        channelMonitorRoutes_.erase (it);
    }

    // Slice E3 — decrement target-bus sender counts for every edge the
    // channel currently contributes to (main-out target if it's a bus,
    // plus each nonzero send into an FX return). graph_.removeNode below
    // tears down the edges; without this scan first the counts would
    // leak (the buses would think they have phantom senders forever).
    if (auto it = channelNodeIds_.find (id.value()); it != channelNodeIds_.end())
    {
        const auto chNode = it->second;
        const auto mainDest = graph_.mainOutOf (chNode);
        adjustBusCountForNode (mainDest, -1, busNodeIds_, buses_);
        for (const auto& e : graph_.sendEdges())
        {
            if (e.source != chNode) continue;
            if (e.level > 0.0f)
                adjustBusCountForNode (e.fxReturn, -1, busNodeIds_, buses_);
        }
        graph_.removeNode (chNode);
        channelNodeIds_.erase (it);
    }

    channels_.erase (id.value());
    channelSources_.erase (id.value());
    channelPreFaderSends_.erase (id.value());

    // V9 Slice 2 — free the post-strip storage. Caller is responsible for
    // not removing a channel while OutputMixer is still reading the pointer
    // on the audio thread (Slice 3's MON-off path tears down the OutputMixer
    // channel BEFORE this call, mirroring the monitor-route teardown above).
    postStrip_.erase (id.value());
}

void InputMixer::setChannelMonitorMode (ChannelId id, MonitorMode mode)
{
    auto chIt = channels_.find (id.value());
    if (chIt == channels_.end()) return;                          // unknown id — silent no-op
    if (chIt->second.signalType != SignalType::Audio) return;     // monitor is audio-only

    auto it = channelMonitorRoutes_.find (id.value());

    if (mode == MonitorMode::Off)
    {
        // Tear down the auto-created OutputMixer channel (if one exists)
        // and drop the per-channel entry so `channelMonitorMode` reads
        // the documented Off default.
        if (it != channelMonitorRoutes_.end())
        {
            if (it->second.outputChannelId.has_value() && outputMixer_ != nullptr)
                outputMixer_->removeChannel (*it->second.outputChannelId);
            channelMonitorRoutes_.erase (it);
        }
        return;
    }

    // mode == On.
    // Idempotent: a second On call while the OutputMixer channel already
    // exists is a no-op (don't mint a duplicate channel).
    if (it != channelMonitorRoutes_.end() && it->second.outputChannelId.has_value())
        return;

    // Without an attached OutputMixer, track the mode so a later
    // `attachOutputMixer` + replay path can engage the channel, but mint
    // nothing in the meantime. Mirrors the prior DirectLayer-unbound
    // policy and the set-once-non-owning invariant on `outputMixer_`.
    if (outputMixer_ == nullptr)
    {
        MonitorRouteState state;
        state.mode = MonitorMode::On;
        channelMonitorRoutes_[id.value()] = std::move (state);
        return;
    }

    // Mint a fresh OutputMixer channel and wire its audio source to this
    // input's post-strip stereo buffer (the V9 Slice 2 seam). Pointers
    // are stable for the input channel's lifetime; OutputMixer reads
    // them every block.
    const auto monChId = outputMixer_->addChannel (SignalType::Audio);
    outputMixer_->setChannelAudioSource (monChId,
                                         postStripPointer (id, 0),
                                         postStripPointer (id, 1));

    MonitorRouteState state;
    state.mode            = MonitorMode::On;
    state.outputChannelId = monChId;
    channelMonitorRoutes_[id.value()] = std::move (state);
}

MonitorMode InputMixer::channelMonitorMode (ChannelId id) const noexcept
{
    if (auto it = channelMonitorRoutes_.find (id.value()); it != channelMonitorRoutes_.end())
        return it->second.mode;
    return MonitorMode::Off;
}

std::optional<OutputChannelId>
InputMixer::channelMonitorOutputChannel (ChannelId id) const noexcept
{
    if (auto it = channelMonitorRoutes_.find (id.value()); it != channelMonitorRoutes_.end())
        return it->second.outputChannelId;
    return std::nullopt;
}

const float* InputMixer::postStripPointer (ChannelId id, int side) const noexcept
{
    if (side < 0 || side > 1) return nullptr;
    auto it = postStrip_.find (id.value());
    if (it == postStrip_.end()) return nullptr;
    return it->second[static_cast<std::size_t> (side)].data();
}

bool InputMixer::channelSendIsPreFader (ChannelId id) const noexcept
{
    if (auto it = channelPreFaderSends_.find (id.value()); it != channelPreFaderSends_.end())
        return it->second != 0;
    return false; // unknown id or never set → safe default (post-fader)
}

void InputMixer::setChannelSendIsPreFader (ChannelId id, bool preFader) noexcept
{
    if (channels_.find (id.value()) == channels_.end()) return; // unknown id
    channelPreFaderSends_[id.value()] = preFader ? 1 : 0;
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
    // Forward the stashed effect-chain host to the newly registered bus so
    // post-`setEffectChainHost` `addBus` calls get the same wiring as
    // pre-existing buses (parity with OutputMixer::addBus). Null is a valid
    // value (M5 inline path).
    buses_.back().setEffectChainHost (effectChainHost_);
    const auto kind = (config.kind == BusKind::FxReturn) ? MixerNodeKind::FxReturn
                                                         : MixerNodeKind::Bus;
    busNodeIds_.push_back (graph_.addNode (kind)); // defaults main-out to primary (Tape)
    return id;
}

BusId InputMixer::addFxReturn (const std::string& name)
{
    return addBus (BusConfig { 2, name, BusKind::FxReturn });
}

void InputMixer::setBusEffectChain (BusId id, EffectChain chain)
{
    for (auto& bus : buses_)
        if (bus.id() == id)
        {
            bus.setEffectChain (std::move (chain));
            return;
        }
}

void InputMixer::setEffectChainHost (IEffectChainHost* host) noexcept
{
    effectChainHost_ = host;
    for (auto& bus : buses_)
        bus.setEffectChainHost (host);
}

Bus* InputMixer::busForId (BusId id) noexcept
{
    for (auto& bus : buses_)
        if (bus.id() == id)
            return &bus;
    return nullptr;
}

bool InputMixer::renameBus (BusId id, std::string newName)
{
    if (id.value() == 0) return false;       // invalid sentinel
    for (auto& bus : buses_)
    {
        if (bus.id() == id)
        {
            bus.setName (std::move (newName));
            return true;
        }
    }
    return false;
}

MixerMainOut InputMixer::mainOutSnapshot (MixerNodeId node) const noexcept
{
    const auto dest = graph_.mainOutOf (node);
    MixerMainOut out;
    const int tapeSlot = tapeSlotForNode (dest);
    if (tapeSlot >= 0)
    {
        out.kind     = MixerMainOut::Kind::Terminal;
        out.terminal = MixerTerminalKind::Tape;
        out.tapeId   = tapeTerminals_[static_cast<std::size_t> (tapeSlot)].tapeId;
        return out;
    }
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput))
    {
        out.kind = MixerMainOut::Kind::Terminal;
        out.terminal = MixerTerminalKind::HardwareOutput;
        return out;
    }
    out.kind = MixerMainOut::Kind::Bus;
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
        if (busNodeIds_[i] == dest)
        {
            out.busId = buses_[i].id().value();
            return out;
        }
    jassertfalse; // graph invariant: dest is a Bus node absent from busNodeIds_
    return out;
}

std::vector<MixerSend> InputMixer::sendSnapshot (MixerNodeId node) const
{
    std::vector<MixerSend> sends;
    for (const auto& edge : graph_.sendEdges())
        if (edge.source == node)
            for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
                if (busNodeIds_[i] == edge.fxReturn)
                {
                    sends.push_back ({ buses_[i].id().value(), edge.level });
                    break;
                }
    return sends;
}

const EffectChain* InputMixer::channelInsertChain (ChannelId id) const noexcept
{
    auto it = channels_.find (id.value());
    if (it == channels_.end() || it->second.processing == nullptr)
        return nullptr;
    if (it->second.signalType != SignalType::Audio)
        return nullptr;
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (it->second.processing.get());
    return &strip->effectChain();
}

InputMixerGraphState InputMixer::exportGraphState() const
{
    InputMixerGraphState state;

    for (std::size_t i = 0; i < buses_.size(); ++i)
    {
        const auto& bus = buses_[i];
        MixerBusState entry;
        entry.busId        = bus.id().value();
        entry.channelCount = bus.config().channelCount;
        entry.name         = bus.config().name;
        entry.kind         = bus.config().kind == BusKind::FxReturn
                                ? MixerBusKind::FxReturn : MixerBusKind::Bus;
        entry.mainOut      = mainOutSnapshot (busNodeIds_[i]);
        entry.sends        = sendSnapshot (busNodeIds_[i]);
        entry.inserts      = bus.effectChain();
        state.buses.push_back (std::move (entry));
    }

    // channels_ is an unordered_map; export in ascending channel-id order so the
    // snapshot (and its round-trip equality) is deterministic.
    std::vector<std::int64_t> ids;
    ids.reserve (channels_.size());
    for (const auto& kv : channels_) ids.push_back (kv.first);
    std::sort (ids.begin(), ids.end());

    for (auto rawId : ids)
    {
        const auto& ch = channels_.at (rawId);
        InputChannelState entry;
        entry.channelId     = ch.id.value();
        entry.signalType    = ch.signalType;
        entry.inputSourceId = ch.source.value();
        entry.tapeMode      = ch.tapeMode;
        if (auto src = channelSources_.find (rawId); src != channelSources_.end())
            entry.source = { src->second.left, src->second.right, src->second.stereo };
        const auto node = channelNodeIds_.at (rawId);
        entry.mainOut = mainOutSnapshot (node);
        entry.sends   = sendSnapshot (node);
        if (auto* chain = channelInsertChain (ch.id)) entry.inserts = *chain;
        if (auto pf = channelPreFaderSends_.find (rawId); pf != channelPreFaderSends_.end())
            entry.preFaderSends = pf->second != 0;
        if (auto mr = channelMonitorRoutes_.find (rawId); mr != channelMonitorRoutes_.end())
        {
            entry.monitorMode = mr->second.mode;
            // V9 collapsed MonitorMode to {Off,On} — there is no per-channel
            // output-pair anymore (the OutputMixer auto-channel inherits the
            // master's hardware-output pair). The legacy field stays in
            // MixerGraphState for V8 back-compat on disk (SessionFormat
            // suppresses the JSON property when the value is 0).
            entry.monitorOutputPair = 0;
        }
        state.channels.push_back (std::move (entry));
    }

    state.nextBusId     = nextBusId_;
    state.nextChannelId = nextChannelId_;
    return state;
}

void InputMixer::importGraphState (const InputMixerGraphState& state)
{
    // 1. Buses / FX returns. The header contract requires a freshly-constructed
    //    mixer, so no bus from the snapshot should already exist. addBus mints
    //    monotonically from nextBusId_ = 1 and the snapshot's `buses` are
    //    serialized in insertion order (= id-ascending), so replay produces busIds
    //    that match the snapshot exactly. Assert the precondition rather than
    //    silently skip-then-apply-chain to a colliding bus: that path would
    //    desync subsequent main-outs/sends/channels which all assume the
    //    snapshot's id space.
    [[maybe_unused]] const auto busExists = [this] (std::int64_t id)
    {
        for (const auto& bus : buses_) if (bus.id().value() == id) return true;
        return false;
    };
    for (const auto& b : state.buses)
    {
        jassert (! busExists (b.busId));
        BusConfig config;
        config.channelCount = b.channelCount;
        config.name         = b.name;
        config.kind         = b.kind == MixerBusKind::FxReturn ? BusKind::FxReturn : BusKind::Bus;
        addBus (config); // mints b.busId (dense) and registers the graph node
        setBusEffectChain (BusId (b.busId), b.inserts);
    }

    // 2. Channels — register with persisted ChannelIds (constructed directly so
    //    removeChannel-induced id gaps round-trip). Mirror addChannel's internals.
    for (const auto& c : state.channels)
    {
        const ChannelId id (c.channelId);
        channels_.emplace (c.channelId,
                           Channel (id, c.signalType, InputId (c.inputSourceId), c.tapeMode));
        channelSources_[c.channelId] = { c.source.left, c.source.right, c.source.stereo };
        channelNodeIds_[c.channelId] = graph_.addNode (MixerNodeKind::Channel);

        // V9 Slice 2 — `importGraphState` bypasses `addChannel`, so the
        // post-strip storage must be allocated here too. Without this the
        // seam returns nullptr for any persisted channel after a project
        // reload, breaking Slice 3's MON-on lookup.
        auto& slot = postStrip_[c.channelId];
        slot[0].assign (kMaxScratchSamples, 0.0f);
        slot[1].assign (kMaxScratchSamples, 0.0f);

        if (c.signalType == SignalType::Audio)
            if (auto* chain = channels_.at (c.channelId).processing.get())
                static_cast<ChannelStrip<SignalType::Audio>*> (chain)->setEffectChain (c.inserts);

        if (c.preFaderSends)
            channelPreFaderSends_[c.channelId] = 1;
    }

    // V9 Slice 3 — replay the per-channel MON mode AFTER sends/main-outs
    // are applied (mode is independent of those, but the strip must exist
    // before its post-strip buffer is wired to the OutputMixer's audio
    // source). When the OutputMixer collaborator isn't attached at import
    // time, the mode is stashed and a later `attachOutputMixer` does NOT
    // auto-replay — callers that load a project before attaching the
    // OutputMixer must replay manually (today MainComponent attaches first).
    // The legacy `monitorOutputPair` is intentionally ignored — V9 has no
    // per-channel output-pair concept.
    for (const auto& c : state.channels)
        if (c.monitorMode != MonitorMode::Off)
            setChannelMonitorMode (ChannelId (c.channelId), c.monitorMode);

    // 3. Apply main-outs (all nodes exist now, so no cycle false-positives).
    for (const auto& b : state.buses)    applyBusMainOut (BusId (b.busId), b.mainOut);
    for (const auto& c : state.channels) applyChannelMainOut (ChannelId (c.channelId), c.mainOut);

    // 4. Apply sends.
    for (const auto& b : state.buses)
        for (const auto& s : b.sends) { const bool ok = setBusSend (BusId (b.busId), BusId (s.busId), s.level); jassert (ok); juce::ignoreUnused (ok); }
    for (const auto& c : state.channels)
        for (const auto& s : c.sends) { const bool ok = setChannelSend (ChannelId (c.channelId), BusId (s.busId), s.level); jassert (ok); juce::ignoreUnused (ok); }

    // 5. Advance id counters — never rewind (the ctor leaves nextBusId_ == 1).
    nextBusId_     = std::max (nextBusId_, state.nextBusId);
    nextChannelId_ = std::max (nextChannelId_, state.nextChannelId);
}

void InputMixer::applyChannelMainOut (ChannelId id, const MixerMainOut& m)
{
    const bool ok = (m.kind == MixerMainOut::Kind::Bus)
                        ? setChannelMainOutToBus (id, BusId (m.busId))
                        : (m.terminal == MixerTerminalKind::HardwareOutput)
                              ? setChannelMainOutToHardwareOutput (id)
                              : setChannelMainOutToTape (id, TapeId (m.tapeId));
    jassert (ok); // a snapshot edge the graph rejected = corrupt/incompatible snapshot
    juce::ignoreUnused (ok);
}

void InputMixer::applyBusMainOut (BusId id, const MixerMainOut& m)
{
    const bool ok = (m.kind == MixerMainOut::Kind::Bus)
                        ? setBusMainOutToBus (id, BusId (m.busId))
                        : (m.terminal == MixerTerminalKind::HardwareOutput)
                              ? setBusMainOutToHardwareOutput (id)
                              : setBusMainOutToTape (id, TapeId (m.tapeId));
    jassert (ok); // a snapshot edge the graph rejected = corrupt/incompatible snapshot
    juce::ignoreUnused (ok);
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

MixerNodeId InputMixer::tapeNodeFor (TapeId id) const noexcept
{
    for (const auto& t : tapeTerminals_)
        if (t.tapeId == id.value()) return t.node;
    return MixerNodeId {};
}

int InputMixer::tapeSlotForNode (MixerNodeId node) const noexcept
{
    for (std::size_t i = 0; i < tapeTerminals_.size(); ++i)
        if (tapeTerminals_[i].node == node) return static_cast<int> (i);
    return -1;
}

bool InputMixer::hasTape (TapeId id) const noexcept { return tapeNodeFor (id).isValid(); }
int  InputMixer::tapeCount() const noexcept { return static_cast<int> (tapeTerminals_.size()); }

bool InputMixer::addTape (TapeId id)
{
    if (hasTape (id)) return false;
    if (tapeTerminals_.size() >= static_cast<std::size_t> (kMaxTapes))
    {
        jassertfalse; // fail loud — silently dropping a tape terminal corrupts routing
        return false;
    }
    tapeTerminals_.push_back ({ id.value(), graph_.addTerminal (MixerTerminal::Tape) });
    return true;
}

bool InputMixer::removeTape (TapeId id)
{
    if (id == TapeId { 1 }) return false;            // primary is permanent
    const MixerNodeId node = tapeNodeFor (id);
    if (! node.isValid()) return false;
    if (! graph_.removeTerminal (node)) return false; // graph also refuses the primary
    tapeTerminals_.erase (std::remove_if (tapeTerminals_.begin(), tapeTerminals_.end(),
                                          [id] (const TapeTerminal& t)
                                          { return t.tapeId == id.value(); }),
                          tapeTerminals_.end());
    return true;
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
    return node.isValid() && tapeSlotForNode (graph_.mainOutOf (node)) >= 0;
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
    if (tapeSlotForNode (dest) >= 0)                                 return MainOutDest::Tape;
    if (dest == graph_.terminalNode (MixerTerminal::HardwareOutput)) return MainOutDest::HardwareOutput;
    return MainOutDest::Bus;
}

BusId InputMixer::busIdForNode (MixerNodeId node) const noexcept
{
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
        if (busNodeIds_[i] == node) return buses_[i].id();
    return BusId { 0 };
}

BusId InputMixer::channelMainOutBus (ChannelId ch) const noexcept
{
    return busIdForNode (graph_.mainOutOf (nodeForChannel (ch)));
}

BusId InputMixer::busMainOutBus (BusId id) const noexcept
{
    return busIdForNode (graph_.mainOutOf (nodeForBus (id)));
}

bool InputMixer::busMainOutToBusWouldCycle (BusId from, BusId to) const noexcept
{
    const MixerNodeId fromNode = nodeForBus (from);
    const MixerNodeId toNode   = nodeForBus (to);
    if (! fromNode.isValid() || ! toNode.isValid()) return false;
    return graph_.wouldMainOutCycle (fromNode, toNode);
}

bool InputMixer::setChannelMainOutToBus (ChannelId ch, BusId bus)
{
    const auto src = nodeForChannel (ch);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = nodeForBus (bus);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
    {
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
        adjustBusCountForNode (newDest, +1, busNodeIds_, buses_);
    }
    return ok;
}
bool InputMixer::setChannelMainOutToHardwareOutput (ChannelId ch)
{
    const auto src = nodeForChannel (ch);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = graph_.terminalNode (MixerTerminal::HardwareOutput);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
    return ok;
}
bool InputMixer::setChannelMainOutToTape (ChannelId ch)
{
    const auto src = nodeForChannel (ch);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = graph_.terminalNode (MixerTerminal::Tape);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
    return ok;
}
bool InputMixer::setBusMainOutToBus (BusId from, BusId to)
{
    const auto src = nodeForBus (from);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = nodeForBus (to);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
    {
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
        adjustBusCountForNode (newDest, +1, busNodeIds_, buses_);
    }
    return ok;
}
bool InputMixer::setBusMainOutToHardwareOutput (BusId bus)
{
    const auto src = nodeForBus (bus);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = graph_.terminalNode (MixerTerminal::HardwareOutput);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
    return ok;
}
bool InputMixer::setBusMainOutToTape (BusId bus)
{
    const auto src = nodeForBus (bus);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = graph_.terminalNode (MixerTerminal::Tape);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
    return ok;
}
bool InputMixer::setChannelMainOutToTape (ChannelId ch, TapeId tape)
{
    const auto src = nodeForChannel (ch);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = tapeNodeFor (tape);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
    return ok;
}
bool InputMixer::setBusMainOutToTape (BusId bus, TapeId tape)
{
    const auto src = nodeForBus (bus);
    const auto oldDest = graph_.mainOutOf (src);
    const auto newDest = tapeNodeFor (tape);
    const bool ok = graph_.setMainOut (src, newDest);
    if (ok && oldDest != newDest)
        adjustBusCountForNode (oldDest, -1, busNodeIds_, buses_);
    return ok;
}
bool InputMixer::channelMainOutIsTape (ChannelId ch, TapeId tape) const noexcept
{
    const MixerNodeId node = tapeNodeFor (tape);
    return node.isValid() && graph_.mainOutOf (nodeForChannel (ch)) == node;
}
bool InputMixer::busMainOutIsTape (BusId bus, TapeId tape) const noexcept
{
    const MixerNodeId node = tapeNodeFor (tape);
    return node.isValid() && graph_.mainOutOf (nodeForBus (bus)) == node;
}
InputMixer::MainOutDest InputMixer::channelMainOut (ChannelId ch) const noexcept
{ return classifyMainOut (graph_.mainOutOf (nodeForChannel (ch))); }
InputMixer::MainOutDest InputMixer::busMainOut (BusId bus) const noexcept
{ return classifyMainOut (graph_.mainOutOf (nodeForBus (bus))); }
bool InputMixer::setChannelSend (ChannelId ch, BusId fxReturn, float level)
{
    const auto src = nodeForChannel (ch);
    const auto tgt = nodeForBus (fxReturn);
    const float oldLevel = graph_.sendLevel (src, tgt);
    const bool ok = graph_.setSend (src, tgt, level);
    if (ok)
    {
        if (oldLevel <= 0.0f && level > 0.0f) adjustBusCountForNode (tgt, +1, busNodeIds_, buses_);
        if (oldLevel >  0.0f && level <= 0.0f) adjustBusCountForNode (tgt, -1, busNodeIds_, buses_);
    }
    return ok;
}
bool InputMixer::setBusSend (BusId source, BusId fxReturn, float level)
{
    const auto src = nodeForBus (source);
    const auto tgt = nodeForBus (fxReturn);
    const float oldLevel = graph_.sendLevel (src, tgt);
    const bool ok = graph_.setSend (src, tgt, level);
    if (ok)
    {
        if (oldLevel <= 0.0f && level > 0.0f) adjustBusCountForNode (tgt, +1, busNodeIds_, buses_);
        if (oldLevel >  0.0f && level <= 0.0f) adjustBusCountForNode (tgt, -1, busNodeIds_, buses_);
    }
    return ok;
}
float InputMixer::channelSendLevel (ChannelId ch, BusId fxReturn) const noexcept
{ return graph_.sendLevel (nodeForChannel (ch), nodeForBus (fxReturn)); }

float InputMixer::busSendLevel (BusId source, BusId fxReturn) const noexcept
{ return graph_.sendLevel (nodeForBus (source), nodeForBus (fxReturn)); }

ProcessingChain* InputMixer::processingChainFor (ChannelId id) noexcept
{
    auto it = channels_.find (id.value());
    if (it == channels_.end()) return nullptr;
    return it->second.processing.get();
}

void InputMixer::accumulateIntoBus (MixerNodeId busNode, const float* left, const float* right,
                                    float level, int numSamples) noexcept
{
    for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
    {
        if (busNodeIds_[i] != busNode) continue;
        float* bl = buses_[i].mixBufferChannel (0);
        float* br = buses_[i].mixBufferChannel (1);
        if (bl == nullptr || br == nullptr) return;
        for (int s = 0; s < numSamples; ++s) { bl[s] += left[s] * level; br[s] += right[s] * level; }
        return;
    }
}

void InputMixer::accumulateIntoTape (int slot, const float* left, const float* right,
                                     float level, int numSamples) noexcept
{
    if (slot < 0 || slot >= static_cast<int> (tapeMixLeft_.size())) return;
    jassert (numSamples >= 0 && numSamples <= static_cast<int> (tapeMixLeft_[static_cast<std::size_t> (slot)].size()));
    float* tl = tapeMixLeft_[static_cast<std::size_t> (slot)].data();
    float* tr = tapeMixRight_[static_cast<std::size_t> (slot)].data();
    for (int s = 0; s < numSamples; ++s) { tl[s] += left[s] * level; tr[s] += right[s] * level; }
    tapeTouched_[static_cast<std::size_t> (slot)] = 1;
}

void InputMixer::renderInputGraph (const float* const* deviceIn, int numDeviceChannels,
                                   float* const* directOut, int numDirectOutChannels,
                                   int numSamples) noexcept
{
    if (deviceIn == nullptr || numDeviceChannels <= 0 || numSamples <= 0) return;
    jassert (directOut != nullptr || numDirectOutChannels == 0); // null ptr with non-zero count would deref
    const int n = std::min (numSamples, static_cast<int> (kMaxScratchSamples));

    const MixerNodeId hwNode = graph_.terminalNode (MixerTerminal::HardwareOutput);

    // Zero only the active tape slots; clear their touched flags.
    const std::size_t tapeSlots = tapeTerminals_.size();
    for (std::size_t i = 0; i < tapeSlots; ++i)
    {
        std::memset (tapeMixLeft_[i].data(),  0, static_cast<std::size_t> (n) * sizeof (float));
        std::memset (tapeMixRight_[i].data(), 0, static_cast<std::size_t> (n) * sizeof (float));
        tapeTouched_[i] = 0;
    }

    // ── Steps 1–2: per channel, gather → strip → route main-out + sends ──
    for (const auto& [chValue, source] : channelSources_)
    {
        auto chIt = channels_.find (chValue);
        if (chIt == channels_.end()) continue;
        const auto& channel = chIt->second;
        if (channel.signalType != SignalType::Audio || channel.processing == nullptr) continue;

        const int leftCh  = source.left;
        const int rightCh = source.stereo ? source.right : source.left;
        if (leftCh  < 0 || leftCh  >= numDeviceChannels) continue;
        if (rightCh < 0 || rightCh >= numDeviceChannels) continue;
        if (deviceIn[leftCh] == nullptr || deviceIn[rightCh] == nullptr) continue;

        const auto byteCount = static_cast<std::size_t> (n) * sizeof (float);
        std::memcpy (scratchLeft_.data(),  deviceIn[leftCh],  byteCount);
        std::memcpy (scratchRight_.data(), deviceIn[rightCh], byteCount);

        // Slice E2: snapshot the pre-strip (pre-fader) signal so the send
        // accumulator can choose unstripped source when a channel is in
        // pre-fader mode. Copy unconditionally — a single block-sized
        // memcpy per channel; sub-µs at default block sizes.
        std::memcpy (scratchLeftPre_.data(),  scratchLeft_.data(),  byteCount);
        std::memcpy (scratchRightPre_.data(), scratchRight_.data(), byteCount);

        float* stereo[2] { scratchLeft_.data(), scratchRight_.data() };
        auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (channel.processing.get());
        strip->process (stereo, 2, n); // also updates peak/LUFS meters

        // V9 Slice 2 — publish the post-strip stereo into the channel's
        // stable seam buffer so Slice 3's OutputMixer channel (cached by
        // pointer) reads the freshly-processed signal this block. RT-safe:
        // member-owned storage sized at `addChannel`, no allocation, no
        // locks, plain memcpy. The slot is guaranteed present because
        // `addChannel` reserves it; defensive lookup keeps the audio
        // thread silent on a race we don't expect to see.
        if (auto psIt = postStrip_.find (chValue); psIt != postStrip_.end())
        {
            const auto bytes = static_cast<std::size_t> (n) * sizeof (float);
            std::memcpy (psIt->second[0].data(), scratchLeft_.data(),  bytes);
            std::memcpy (psIt->second[1].data(), scratchRight_.data(), bytes);
        }

        const MixerNodeId chNode = nodeForChannel (channel.id);
        const MixerNodeId dest   = graph_.mainOutOf (chNode);
        const int tapeSlot       = tapeSlotForNode (dest);

        if (tapeSlot >= 0)
        {
            if (channel.tapeMode != TapeMode::NoTape)
                accumulateIntoTape (tapeSlot, scratchLeft_.data(), scratchRight_.data(), 1.0f, n);
        }
        else if (dest == hwNode)
        {
            for (int c = 0; c < std::min (numDirectOutChannels, 2); ++c)
            {
                float* o = directOut[c];
                if (o == nullptr) continue;
                const float* src = (c == 0) ? scratchLeft_.data() : scratchRight_.data();
                for (int s = 0; s < n; ++s) o[s] += src[s];
            }
        }
        else // a bus
        {
            accumulateIntoBus (dest, scratchLeft_.data(), scratchRight_.data(), 1.0f, n);
        }

        // Slice E2: per-channel send source — pre-strip when the channel's
        // pre-fader flag is set, post-strip otherwise (today's default).
        const bool preFader =
            [this, val = channel.id.value()] {
                auto it = channelPreFaderSends_.find (val);
                return it != channelPreFaderSends_.end() && it->second != 0;
            }();
        const float* const sendLeft  = preFader ? scratchLeftPre_.data()  : scratchLeft_.data();
        const float* const sendRight = preFader ? scratchRightPre_.data() : scratchRight_.data();

        for (const auto& e : graph_.sendEdges())
        {
            if (e.source != chNode) continue;
            accumulateIntoBus (e.fxReturn, sendLeft, sendRight, e.level, n);
        }
    }

    // ── Step 3: process each bus / FX return into its main-out destination ──
    // RT-safety: evaluationOrder() is a const& to a pre-built vector (no alloc);
    // busNodeIds_ lookups are bounded linear scans; no graph mutators are called.
    for (const MixerNodeId nodeId : graph_.evaluationOrder())
    {
        std::size_t busIdx = busNodeIds_.size();
        for (std::size_t i = 0; i < busNodeIds_.size(); ++i)
            if (busNodeIds_[i] == nodeId) { busIdx = i; break; }
        if (busIdx >= busNodeIds_.size()) continue; // channel or terminal node

        const Bus& bus        = buses_[busIdx];

        // Slice E3 — send-zero bypass: skip the whole per-bus DSP when no
        // active senders. Mirrors the OutputMixer bypass.
        if (! bus.sendInputActive()) continue;

        // Bus-to-FX-return send tap — accumulate this bus's mixBuffer ×
        // level into each FX-return target's mixBuffer (mirror of the
        // OutputMixer step-3 tap). Tap is post-channel-sends, pre-bus-
        // effect-chain. Topo-sort guarantees FX-return targets process
        // after this bus; `MixerGraph::setSend` rejects cycles.
        {
            const float* const myL = bus.mixBufferChannel (0);
            const float* const myR = bus.mixBufferChannel (1);
            if (myL != nullptr && myR != nullptr)
            {
                for (const auto& edge : graph_.sendEdges())
                {
                    if (edge.source != nodeId) continue;
                    if (edge.level <= 0.0f)    continue;

                    std::size_t tgt = busNodeIds_.size();
                    for (std::size_t ti = 0; ti < busNodeIds_.size(); ++ti)
                        if (busNodeIds_[ti] == edge.fxReturn) { tgt = ti; break; }
                    if (tgt >= busNodeIds_.size()) continue;

                    float* const tL = buses_[tgt].mixBufferChannel (0);
                    float* const tR = buses_[tgt].mixBufferChannel (1);
                    if (tL == nullptr || tR == nullptr) continue;

                    const float lvl = edge.level;
                    for (int s = 0; s < n; ++s)
                    {
                        tL[s] += myL[s] * lvl;
                        tR[s] += myR[s] * lvl;
                    }
                }
            }
        }

        const MixerNodeId dest = graph_.mainOutOf (nodeId);
        const int tapeSlot     = tapeSlotForNode (dest);

        if (tapeSlot >= 0)
        {
            std::memset (scratchLeft_.data(),  0, static_cast<std::size_t> (n) * sizeof (float));
            std::memset (scratchRight_.data(), 0, static_cast<std::size_t> (n) * sizeof (float));
            float* sc[2] { scratchLeft_.data(), scratchRight_.data() };
            bus.process (sc, 2, n);
            accumulateIntoTape (tapeSlot, scratchLeft_.data(), scratchRight_.data(), 1.0f, n);
        }
        else if (dest == hwNode)
        {
            float* dp[2] { (numDirectOutChannels > 0 ? directOut[0] : nullptr),
                           (numDirectOutChannels > 1 ? directOut[1] : nullptr) };
            bus.process (dp, std::min (numDirectOutChannels, 2), n);
        }
        else // another bus
        {
            for (std::size_t di = 0; di < busNodeIds_.size(); ++di)
            {
                if (busNodeIds_[di] != dest) continue;
                float* dp[2] { buses_[di].mixBufferChannel (0), buses_[di].mixBufferChannel (1) };
                bus.process (dp, 2, n);
                break;
            }
        }
    }

    // ── Step 4: deliver each tape that received signal, summed, once. ──
    if (tapeSink_ != nullptr)
        for (std::size_t i = 0; i < tapeSlots; ++i)
            if (tapeTouched_[i])
                tapeSink_->deliverTapeBlock (TapeId { tapeTerminals_[i].tapeId },
                                             tapeMixLeft_[i].data(),
                                             tapeMixRight_[i].data(), n);
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
                                       // lands in M11 IAF
    partialFile.deleteFile();
}

} // namespace ida
