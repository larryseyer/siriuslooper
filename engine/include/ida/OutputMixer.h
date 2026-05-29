#pragma once

#include "ida/Bus.h"
#include "ida/Channel.h"
#include "ida/ChannelStrip.h"
#include "ida/EffectChain.h"
#include "ida/IEffectChainHost.h"
#include "ida/MixerGraph.h"
#include "ida/MixerGraphState.h"
#include "ida/SignalType.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ida
{

/// V3 §2.2 / V7 alignment plan M5: the output-side mixer. Symmetric to
/// `InputMixer`; sits between Constituent rendering and the physical
/// output layer, owns the output-channel registry + per-channel strips,
/// and (in M5 Session 3) dispatches the audio-thread render through
/// per-channel strips, send/return architecture, and session-level
/// effect buses including the master bus.
///
/// Session 3 (this file) fills the audio-thread render body. The four-step
/// traversal: (1) scratch-mix each registered output channel from its
/// input source through its `ChannelStrip<Audio>`; (2) for each
/// (channel, bus) send level > 0, accumulate the scaled scratch into the
/// target bus's mixBuffer; (3) for each non-master bus in topological
/// evaluation order (sources before destinations, from the routing graph),
/// invoke `Bus::process` which routes the bus mixBuffer into its graph
/// main-out destination — the master bus, a subgroup bus, or the terminal —
/// at unity; (4) the master bus writes its mixBuffer additively into the
/// physical output channels.
///
/// M5 auto-registration policy: OutputMixer comes up EMPTY. MainComponent
/// does not auto-register channels — operator UX for mixer config is
/// M22+. Result: in M5, the OutputMixer path is a true no-op until tests
/// or future code calls `addChannel`. This is intentional. V9 Slice 3's
/// per-channel MON auto-creates OutputMixer channels whose audio source
/// reads the matching InputMixer channel's post-strip stereo buffer (the
/// only sanctioned input → output path per whitepaper V9 §5.2 / §7.2);
/// OutputMixer is otherwise the produced-mix path that becomes meaningful
/// once Constituents render into channels (M6+).
///
/// Threading contract (inherited from M4, see continue.md constraint #6):
/// every configuration mutator (`addChannel`, `setChannelStrip`, `addBus`,
/// `setBusEffectChain`, `routeChannelToBus`) is message-thread only and
/// must complete before the audio thread starts calling `renderBuffer`.
/// There is no atomic-snapshot publish in M5 — operator-facing mutation
/// during a live audio callback is a post-M5 concern.
class OutputMixer
{
public:
    /// Hard ceiling for the pre-allocated send-level matrix. 32 channels
    /// covers any realistic IDA session; raises require re-auditing
    /// RT_SAFETY_CONTRACT §6 for OutputMixer::renderBuffer once S3 lands.
    static constexpr int kMaxOutputChannels = 32;

    /// Hard ceiling for the bus registry. 64 buses comes from V3 (per
    /// continue.md RT-safety note line 165: "the V3 decision caps buses
    /// at 64"). Includes the master bus at `BusId{0}`.
    static constexpr int kMaxBuses = 64;

    /// Output-side equivalent of `InputMixer::MainOutDest`. The Output Mixer
    /// has no Tape terminal (tape is the input side's concern), so the
    /// possible categories are Bus (the main-out is another bus, possibly
    /// the master) and HardwareOutput (direct out, bypassing master). Used
    /// by both `busMainOut` and the slice-E3 `channelMainOut`; declared at
    /// the top of the class so member declarations later in the file can
    /// reference it in their signatures and storage.
    enum class MainOutDest { Bus, HardwareOutput };

    OutputMixer();
    ~OutputMixer();

    // Channel registry --------------------------------------------------------

    /// Registers a new output channel. The signal-type argument is held for
    /// the Session 3 audio-thread dispatch (which routes Audio channels
    /// through `ChannelStrip<Audio>` and skips other modalities until their
    /// real-DSP milestones land). Returns the fresh OutputChannelId — the
    /// promoted return type per V7 plan M5 (was a placeholder `ChannelId`
    /// in the M2 skeleton; promoted now per continue.md constraint #1).
    OutputChannelId addChannel (SignalType type);

    /// Message-thread setter — the OutputMixer takes ownership of the strip.
    /// Each registered output channel gets its own strip; the strip is the
    /// per-channel gain/pan/EQ/dynamics processor that S3 invokes from the
    /// audio thread. Calling with an unknown OutputChannelId is a no-op.
    void setChannelStrip (OutputChannelId id,
                          std::unique_ptr<ChannelStrip<SignalType::Audio>> strip);

    /// Removes a previously-registered output channel and returns its id to a
    /// free-list so the next `addChannel` reuses it (slice 5a). Without
    /// reuse, a session of phrase add/remove churn would burn through
    /// `kMaxOutputChannels = 32` even though the live channel count stays
    /// bounded. Implementation is swap-erase from the parallel vectors
    /// (`channels_`, `channelNodeIds_`, `channelHardwareOutPair_`) plus a
    /// zero of the freed channel's row of `sendMatrix_` so a re-minted id
    /// starts from `addChannel`'s defaults rather than the removed channel's
    /// previous sends. Unknown id → silent no-op (the UI never asks for
    /// unknown ids in steady state). Message-thread only.
    void removeChannel (OutputChannelId id);

    /// Pair-indexed routing of an output channel direct to the HardwareOutput
    /// terminal (slice 5a). Mirror of `setBusMainOutToHardwareOutput`. The
    /// audio-thread render path is NOT touched in 5a — phrase channels
    /// don't feed audio yet; this stores the operator intent so 5b's UI can
    /// surface it and the eventual render-path milestone can read it.
    /// Negative `pairIndex` clamps to 0. Returns false for unknown ids.
    bool setChannelMainOutToHardwareOutput (OutputChannelId channel, int pairIndex);

    /// Reads channel `id`'s recorded hardware-output pair index. Returns 0
    /// for unknown ids — same defensive default as `busHardwareOutPair`.
    /// Message-thread accessor.
    int channelMainOutHardwareOutPair (OutputChannelId id) const noexcept;

    /// S6 (2026-05-29) — sets the OTTO-source provenance marker for `channel`.
    /// -1 = phrase channel; 0..31 = OTTO output index; -2 reserved for the
    /// S7 OTTO Stereo Mix sentinel. The engine never reads this at runtime —
    /// it is purely transport metadata for `exportGraphState`/`importGraphState`
    /// so MainComponent's post-import rebind pass can identify OTTO channels
    /// and rebind their buffer pointers. No-op for unknown ids. Message-thread.
    void setOttoSource (OutputChannelId channel, int ottoSource) noexcept;

    /// Reads channel `id`'s recorded OTTO-source marker. Returns -1 for
    /// unknown ids (the phrase-channel default — same defensive default as
    /// `channelMainOutHardwareOutPair`). Message-thread accessor.
    int channelOttoSource (OutputChannelId id) const noexcept;

    /// Slice E3 (2026-05-24) — output-channel main-out manifest. Mirror of
    /// `busMainOut`. Returns Bus when the channel routes into a bus (master
    /// or aux); HardwareOutput when the channel goes direct to a physical
    /// pair (slice 5a `setChannelMainOutToHardwareOutput`). Unknown ids
    /// return the safe `Bus` sentinel.
    ///
    /// This accessor replaces the slice-5b inference rule ("if every send
    /// is zero, then HardwareOutput") — the picker label now reads this
    /// directly. `routeChannelToBus` with a `BusKind::Bus` target is
    /// radio-style (sets main-out, zeros other Bus-kind sends, keeps
    /// FX-return sends); with `BusKind::FxReturn` it's a plain send tap
    /// and the main-out is untouched.
    MainOutDest channelMainOut (OutputChannelId id) const noexcept;

    /// When `channelMainOut(id) == Bus`, returns the target BusId. Returns
    /// `BusId{0}` (master, the safe default) for unknown ids or for
    /// channels whose main-out is HardwareOutput.
    BusId channelMainOutBus (OutputChannelId id) const noexcept;

    /// Non-owning view of the audio ChannelStrip attached to `id` via
    /// `setChannelStrip`. Returns nullptr for unknown ids or for ids whose
    /// strip hasn't been attached yet. Mirrors `busForId` — the OutputMixer
    /// owns the unique_ptr; this hands out the raw pointer for message-thread
    /// reads (UI driving the pan/width detail panel, gain/mute relays, etc.).
    /// NOT for the audio thread.
    ChannelStrip<SignalType::Audio>* audioStripForChannel (OutputChannelId id) noexcept;

    // Bus and send/return -----------------------------------------------------

    /// Adds a session-level effect bus and returns its fresh BusId starting
    /// at `BusId{1}` (`BusId{0}` is the master bus, auto-created in the
    /// constructor). Pre-allocates the Bus's mix scratch in its constructor
    /// — no audio-thread allocation. Returns `BusId{0}` if the bus registry
    /// is full (caller asked for more than `kMaxBuses - 1` aux buses); the
    /// master bus is always present so this is a safe sentinel.
    BusId addBus (BusConfig config);

    /// Message-thread setter — copies the chain into the named bus. No-op
    /// if the BusId is unknown.
    void setBusEffectChain (BusId id, EffectChain chain);

    /// Message-thread setter — forwards the audio-thread effect-chain
    /// host to every bus currently registered AND stashes the pointer so
    /// any future `addBus` call wires its new bus up at registration
    /// time (M7 S3). Pass `nullptr` to disable dispatch — every bus
    /// then falls back to the M5 inline path.
    ///
    /// Set-once before the audio thread starts (same contract as
    /// `setBusEffectChain`). The OutputMixer does NOT own the host;
    /// lifetime is the caller's responsibility (test, or future
    /// MainComponent wiring post-M7).
    void setEffectChainHost (IEffectChainHost* host) noexcept;

    /// Message-thread accessor — the live Bus for `id`, or nullptr if unknown.
    /// Mirrors `InputMixer::busForId`. The Output Mixer UI uses this to drive
    /// a bus strip's fader/mute and read its peak/LUFS meter at refresh rate.
    /// NOT for the audio thread (the bus is held by value in a reserved
    /// vector; the pointer is stable for the bus's lifetime within this mixer).
    Bus* busForId (BusId id) noexcept;

    /// Sets the send level from `channel` into `bus`. `sendLevel` is linear
    /// in [0, 1]; values outside the range are clamped. Routing to
    /// `BusId{0}` (master) sets the channel's master direct level; newly
    /// registered channels default to 1.0 into the master so they're
    /// audible at unity without explicit configuration. No-op if either id
    /// is unknown.
    void routeChannelToBus (OutputChannelId channel, BusId bus, float sendLevel);

    /// Routes a bus's main-out into another bus (subgroup) via the routing
    /// graph. Returns false if either id is unknown or the assignment would
    /// create a cycle (delegates to MixerGraph::setMainOut). Default main-out
    /// for an aux bus is the master.
    bool routeBusToBus (BusId from, BusId to);

    /// Routes a bus's main-out direct to the HardwareOutput terminal,
    /// bypassing master. Mirrors `InputMixer::setBusMainOutToHardwareOutput`.
    /// Returns false if the BusId is unknown. The master bus's main-out is
    /// always the terminal; this single-arg overload forwards to the pair-
    /// indexed overload with `pairIndex = 0` (preserves the existing
    /// behaviour for master and aux alike).
    bool setBusMainOutToHardwareOutput (BusId bus);

    /// Pair-indexed routing to the HardwareOutput terminal. `pairIndex` is
    /// the stereo-pair offset into the physical output buffer: 0 = outputs
    /// [0,1], 1 = outputs [2,3], … Accepts the master bus (`BusId{0}`)
    /// because multi-output interfaces let the operator park master on any
    /// pair. Negative values clamp to 0; the audio thread bounds-checks at
    /// render time and falls back to pair 0 if the device's output channel
    /// count shrinks below the requested pair. Returns false on unknown
    /// bus id.
    bool setBusMainOutToHardwareOutput (BusId bus, int pairIndex);

    /// Reads bus `id`'s recorded hardware-output pair index. Returns 0 for
    /// unknown ids and for buses whose main-out is currently a Bus
    /// destination (the recorded index survives switching to a bus dest and
    /// back). Message-thread accessor.
    int busHardwareOutPair (BusId id) const noexcept;

    /// Mirrors `InputMixer::busMainOutToBusWouldCycle`. Delegates to
    /// `MixerGraph::wouldMainOutCycle`. False for unknown ids. The Output
    /// Mixer UI calls this to pre-filter cycle-bound bus destinations from
    /// the per-strip picker so the operator never sees a target the engine
    /// would silently reject. Message-thread only.
    bool busMainOutToBusWouldCycle (BusId from, BusId to) const noexcept;

    /// Renames a bus. Writes the new name into `BusConfig::name`. Returns
    /// false for the master bus (`BusId{0}` — master's name is canonical)
    /// and for unknown ids. Message-thread only (the underlying string is
    /// not guarded against the audio thread; do not call mid-render).
    bool renameBus (BusId id, std::string newName);

    /// Total bus count (master + aux). Useful for cap checks at the UI
    /// layer before calling `addBus`. Mirrors `InputMixer::busCount`.
    int busCount() const noexcept;

    /// V9 Slice 3 diagnostic — total live output-channel count (after any
    /// `removeChannel` cleanups). Used by tests that pin the MON-owned
    /// auto-channel lifecycle: MON off ⇒ no auto channel, MON on ⇒ exactly
    /// one auto channel per input, idempotent re-entry, removeChannel
    /// cleanup. Message-thread accessor.
    int channelCount() const noexcept;

    /// Indexed bus accessor (0..busCount()-1). Index 0 is the master.
    /// Returns the invalid sentinel `BusId{0}` for out-of-range indices
    /// — same defensive default as `InputMixer::busIdAt`. Message-thread.
    BusId busIdAt (int index) const noexcept;

    /// Indexed bus-kind accessor (mirror of `InputMixer::busKindAt`). The
    /// kind distinguishes plain aux buses from FX-return buses so the UI
    /// can pre-filter pickers (channels' main-out picker excludes FX
    /// returns; channels' sends tab includes FX returns only). The master
    /// reports `BusKind::Bus`. Out-of-range indices return the safe `Bus`
    /// sentinel rather than asserting (callers must not branch destructively
    /// on an unknown index). Message-thread.
    BusKind busKindAt (int index) const noexcept;

    /// Reads bus `id`'s current main-out category. Returns Bus when the id
    /// is unknown — same defensive default as InputMixer (callers must
    /// treat it as "ask again later" rather than acting on it).
    MainOutDest busMainOut (BusId id) const noexcept;

    /// When `busMainOut(id) == Bus`, returns the target BusId. Returns
    /// BusId{0} (master) for unknown ids or for buses whose main-out is
    /// HardwareOutput (caller should branch on busMainOut first).
    BusId busMainOutBus (BusId id) const noexcept;

    /// Message-thread accessor for the send-level matrix entry. Returns 0
    /// if either id is unknown. Primary use: tests + S3 audio-thread
    /// traversal that reads send levels into the mix.
    float sendLevelFor (OutputChannelId channel, BusId bus) const noexcept;

    /// Sets a bus-to-FX-return send level (mirror of `InputMixer::setBusSend`).
    /// `source` must be a registered bus (master or aux). `fxReturn` must be
    /// a registered FX-return-kind bus — sends into a plain `Bus` are not
    /// allowed (use `routeBusToBus` for subgroup main-out). Self-sends and
    /// graph cycles are rejected by `MixerGraph::setSend`. `level` is
    /// clamped to [0, 1]; level <= 0 removes the edge. Returns false on
    /// unknown ids, wrong target kind, self-send, or cycle. The fxReturn's
    /// active-sender count is bumped/decremented to keep the send-zero
    /// bypass honest. Message-thread only.
    bool setBusSend (BusId source, BusId fxReturn, float level);

    /// Bus-to-FX-return send level (linear 0..1). Mirror of
    /// `InputMixer::busSendLevel`. Returns 0 when either id is unknown or
    /// no edge exists. Message-thread accessor.
    float busSendLevel (BusId source, BusId fxReturn) const noexcept;

    /// Slice E2 (2026-05-24) — per-channel pre/post-fader send mode.
    /// Default false = post-fader (the channel's strip applies before the
    /// send tap; mute and gain silence/attenuate the send too — today's
    /// behavior). True = pre-fader (the send tap samples the channel's
    /// pre-strip scratch, so a muted channel still feeds its FX returns —
    /// reverb-on-cans / live-cue use cases). One toggle per channel covers
    /// all of that channel's sends (per-send toggle is a future polish
    /// slice per the mixer-symmetry spec). Unknown ids return false.
    bool channelSendIsPreFader (OutputChannelId channel) const noexcept;

    /// Message-thread setter for `channelSendIsPreFader`. Unknown ids are
    /// a silent no-op (defensive — the UI never asks for unknown ids in
    /// steady state). Set before the audio device starts; the audio thread
    /// only reads.
    void setChannelSendIsPreFader (OutputChannelId channel, bool preFader) noexcept;

    // Persistence (routing-graph Phase 5) -------------------------------------

    /// Message-thread snapshot of the routing graph for persistence (Phase 5).
    /// buses[0] is the master (BusId 0). Message-thread only.
    OutputMixerGraphState exportGraphState() const;

    /// Message-thread reconstruction from a snapshot. The master bus already
    /// exists (ctor); its insert chain is applied, never re-added. Call on a
    /// freshly-constructed mixer. Message-thread only.
    void importGraphState (const OutputMixerGraphState&);

    // Mix snapshots (Constituent subtype — lands with the MixSnapshot work)
    // M6+: SnapshotId captureSnapshot (string name);
    // M6+: void recallSnapshot (SnapshotId, TransitionType, Duration);

    // Audio-thread interface (real-time safe in M5 Session 3+) ---------------

    /// Per-channel audio source pointer (2026-05-24). Message-thread setter;
    /// read on the audio thread by `renderBuffer` Step 1 to fill the channel's
    /// scratch. nullptr (default for every newly-added channel) = silent —
    /// the channel produces zero into the bus mix until something feeds it.
    /// Either pointer null = the channel is treated as silent (no partial-
    /// stereo source). Pointers MUST outlive every `renderBuffer` call until
    /// cleared via setChannelAudioSource(id, nullptr, nullptr); the Constituent
    /// renderer slice wires its rendered output here. Unknown id = silent no-op.
    ///
    /// Whitepaper V9 §5.2 / §6 / §7.2: input → output is sanctioned ONLY via
    /// an auto-created OutputMixer channel whose audio source reads the
    /// matching InputMixer channel's post-strip stereo buffer (the operator's
    /// per-channel MON button). OutputMixer phrase channels render Constituent
    /// audio, not live input. This API makes the source explicit; the prior
    /// auto-mapping from `inputChannelData[id-1]` ("M5 proxy") leaked live
    /// input to master independent of the MON button and was removed.
    void setChannelAudioSource (OutputChannelId,
                                const float* left, const float* right) noexcept;

    /// Audio-thread render entry. M5 Session 3 body: channel-strip →
    /// send-matrix → bus-process → master-bus → output traversal.
    ///
    /// `inputChannelData` / `numInputChannels` are RESERVED for a future
    /// audio-source path; renderBuffer currently does not read them. The
    /// per-channel audio source comes from `setChannelAudioSource` instead.
    ///
    /// `const noexcept` per V7 plan line 386 + continue.md constraint #4:
    /// `renderBuffer` is a function of state, not a state mutator. All
    /// state mutation lives in message-thread setters; the audio thread
    /// only reads.
    ///
    /// Output write semantics: ADDITIVE into the physical `output[c]`
    /// buffers. The caller (AudioCallback) zeroes the outputs once at the
    /// start of the buffer so this can safely accumulate.
    ///
    /// The signature stays JUCE-free for the same reason `ChannelStrip::
    /// process` does.
    void renderBuffer (const float* const* inputChannelData,
                       int                 numInputChannels,
                       float* const*       outputChannelData,
                       int                 numOutputChannels,
                       int                 numSamples) const noexcept;

private:
    struct ChannelEntry
    {
        OutputChannelId id;
        SignalType      signalType;
        std::unique_ptr<ChannelStrip<SignalType::Audio>> strip;
    };

    /// Indexes into `sendMatrix_` for the (channel, bus) pair. Channel
    /// indices are derived from `OutputChannelId.value() - 1` because the
    /// id stream starts at 1 (id 0 is reserved). Bus indices are
    /// `BusId.value()` directly because the master bus is `BusId{0}`.
    /// Returns `kMaxOutputChannels * kMaxBuses` (out of bounds) when
    /// either id is past the corresponding ceiling — callers must
    /// bounds-check before indexing.
    std::size_t sendMatrixIndex (OutputChannelId channel, BusId bus) const noexcept;

    /// Shared body of addChannel and importGraphState. Wires every parallel
    /// vector for a freshly-registered channel at `id`, defaults its
    /// sendMatrix row to unity-into-master, and bumps master's active-sender
    /// counter. addChannel allocates `id` (free-list or counter);
    /// importGraphState passes the persisted id verbatim so removeChannel-
    /// induced gaps round-trip.
    void registerChannelWithId (SignalType type, OutputChannelId id);

    MixerGraph                graph_ { MixerTerminal::HardwareOutput };
    std::vector<MixerNodeId>  channelNodeIds_; // parallel to channels_
    std::vector<MixerNodeId>  busNodeIds_;     // parallel to buses_ (index 0 = master)

    std::vector<ChannelEntry> channels_;
    std::vector<Bus>          buses_;

    /// Per-bus hardware-output pair index, parallel to `buses_`. Default 0
    /// (outputs [0,1]) for every newly-added bus. Updated on the message
    /// thread by `setBusMainOutToHardwareOutput`; read on the audio thread
    /// by `renderBuffer` to compute the destination channel offset.
    /// Reserved to `kMaxBuses` in the ctor so `push_back` in `addBus` never
    /// reallocates (matches the `buses_` reservation pattern).
    std::vector<int>          busHardwareOutPair_;

    /// Per-channel hardware-output pair index, parallel to `channels_`
    /// (slice 5a). Default 0 for every newly-added channel. Updated on the
    /// message thread by `setChannelMainOutToHardwareOutput`. Not read by
    /// `renderBuffer` in 5a — phrase channels don't feed audio yet — but
    /// reserved on the same wiring so the eventual render-path milestone
    /// inherits the storage. Reserved to `kMaxOutputChannels` in the ctor
    /// so `push_back` in `addChannel` never reallocates.
    std::vector<int>          channelHardwareOutPair_;

    /// Per-channel main-out kind, parallel to `channels_` (slice E3). Values
    /// match `MainOutDest::Bus` / `MainOutDest::HardwareOutput`. Default
    /// Bus for a freshly-added channel (since addChannel seeds the master
    /// send at unity). Updated by `routeChannelToBus` (Bus-kind target →
    /// Bus) and `setChannelMainOutToHardwareOutput` (→ HardwareOutput).
    std::vector<MainOutDest>  channelMainOutKind_;

    /// Per-channel main-out target bus when `channelMainOutKind_[i] == Bus`,
    /// parallel to `channels_` (slice E3). Default `BusId{0}` (master).
    /// Stable across switches to HardwareOutput and back — the picker UI
    /// can remember the last bus pick.
    std::vector<BusId>        channelMainOutBus_;

    /// S6 (2026-05-29) — per-channel OTTO-source provenance marker. -1 = phrase
    /// channel; 0..31 = OTTO output index. Parallel to `channels_` /
    /// `channelHardwareOutPair_` / `channelMainOutKind_` — push_back/swap-erase
    /// in lockstep. Sized to `kMaxOutputChannels` in the ctor so `push_back` in
    /// `addChannel` never reallocates.
    std::vector<int>          channelOttoSource_;

    /// Per-channel audio source pointers, parallel to `channels_`
    /// (2026-05-24). Default `{nullptr, nullptr}` (silent) for every newly-
    /// added channel; updated via `setChannelAudioSource`. Read on the audio
    /// thread by `renderBuffer` Step 1; if either side is null the channel's
    /// scratch is left zero (the architectural silence rule — phrase channels
    /// are silent until a Constituent renderer / tape playback feeds them).
    /// Reserved to `kMaxOutputChannels` in the ctor so `push_back` in
    /// `addChannel` never reallocates.
    struct ChannelSource
    {
        const float* left  { nullptr };
        const float* right { nullptr };
    };
    std::vector<ChannelSource> channelAudioSources_;

    /// Per-channel pre-fader-send mode, parallel to `channels_` (slice E2).
    /// `char` not `bool` so `std::vector<bool>`'s bit-packing doesn't get
    /// in the way of the audio thread's read pattern. Default 0 (post-
    /// fader). Updated on the message thread by `setChannelSendIsPreFader`;
    /// read on the audio thread by `renderBuffer` to pick the source for
    /// the send-matrix accumulator. Reserved to `kMaxOutputChannels` in
    /// the ctor so `push_back` in `addChannel` never reallocates.
    std::vector<char>         channelPreFaderSends_;

    /// Free-list of OutputChannelId values released by `removeChannel`
    /// (slice 5a). `addChannel` pops from here before incrementing
    /// `nextOutputChannelId_`, so phrase add/remove churn doesn't burn
    /// through `kMaxOutputChannels = 32`. Reserved to `kMaxOutputChannels`
    /// in the ctor so the free-list never reallocates.
    std::vector<std::int64_t> freeChannelIds_;

    /// Dense send-level matrix sized `kMaxOutputChannels * kMaxBuses` in
    /// the constructor. 32 × 64 = 2048 floats = 8 KB total — small enough
    /// to fit comfortably in L1 for the S3 audio-thread traversal.
    std::vector<float>        sendMatrix_;

    /// Per-channel post-strip scratch — sized
    /// `kMaxOutputChannels * kMaxBlockSamples` in the constructor. The
    /// audio-thread `renderBuffer` writes the post-`ChannelStrip` signal
    /// for each registered output channel here, then reads back from it
    /// during the send-matrix accumulation step. `mutable` because
    /// `renderBuffer` is `const` (per V7 plan line 386) but the scratch
    /// is implementation detail the audio thread owns end-to-end. Matches
    /// the `InputMixer::processingScratch_` pattern.
    mutable std::vector<float> channelScratch_;

    /// Per-channel PRE-strip (pre-fader) scratch — same shape as
    /// `channelScratch_` (slice E2). The audio-thread `renderBuffer`
    /// snapshots the source signal into here BEFORE calling
    /// `ChannelStrip::process` on `channelScratch_`. When a channel's
    /// `channelPreFaderSends_` is true, Step 2 reads the send-matrix
    /// accumulator source from this buffer instead of the post-strip
    /// buffer — so mute + gain on the strip don't apply to the send tap.
    /// Allocated unconditionally in the ctor (2 MB total at the default
    /// caps: 32 ch × 2 strip ch × 8192 samples × 4 bytes) rather than
    /// conditional-on-first-toggle because the audio-thread atomic
    /// publish for a newly-allocated buffer adds RT-safety risk for a
    /// pre-audio-start memory saving that isn't load-bearing. Same
    /// `mutable` rationale as `channelScratch_`.
    mutable std::vector<float> channelPreFaderScratch_;

    std::int64_t nextOutputChannelId_ { 1 };
    std::int64_t nextBusId_ { 1 }; // BusId{0} is the master, auto-created.

    /// Stashed `IEffectChainHost*` (M7 S3) — null = no audio-thread
    /// dispatch, every bus falls back to the M5 inline path. Forwarded
    /// to every bus by `setEffectChainHost`, and to any new bus on
    /// `addBus`. Lifetime is the caller's responsibility.
    IEffectChainHost* effectChainHost_ { nullptr };
};

} // namespace ida
