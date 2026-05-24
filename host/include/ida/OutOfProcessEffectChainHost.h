#pragma once

#include "ida/EffectChain.h"
#include "ida/IEffectChainHost.h"
#include "ida/IInternalFxAdapter.h"
#include "ida/INotificationSink.h"
#include "ida/InternalFxFactory.h"
#include "ida/InternalFxId.h"
#include "ida/OutOfProcessPluginInstance.h"
#include "ida/PluginDescriptor.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ida
{

/// Out-of-process concrete implementation of `IEffectChainHost` (M7 S3 + S4).
/// Owns one `OutOfProcessPluginInstance` per active slot of an
/// `EffectChain` on a given `Bus`. Each slot mints a CLAP-mode host child
/// at `configureBus(...)` time on the message thread; the audio thread
/// later calls `pumpSlot(...)` to ship one buffer through the slot's IPC
/// rings.
///
/// **S4 additions — watchdog + supervisor + plug-in NotificationBus events.**
/// Every slot carries an atomic miss counter that `pumpSlot` increments on
/// any push-or-pop failure (ring full / ring empty / SPSC backpressure).
/// A single owned supervisor thread polls the miss counter at 50 ms cadence
/// and, on `kConsecutiveMissThreshold` misses, performs a bypass-flag
/// fenced restart of the child:
///
///   1. Sets the slot's `bypassed` flag (atomic release). `pumpSlot` reads
///      it on the next call and short-circuits to "dry on miss" without
///      touching the old instance.
///   2. Sleeps `kRestartGraceMs` to guarantee any in-flight `pumpSlot`
///      call finished (audio buffer << 100 ms; pumpSlot itself < 1 ms).
///   3. Tears down the old `OutOfProcessPluginInstance` (its destructor
///      SIGTERMs + SIGKILLs + reaps the child cleanly).
///   4. Constructs a new instance against the same descriptor / host
///      binary / clap bundle stored in the slot's state.
///   5. Clears the `bypassed` flag (atomic release). The next `pumpSlot`
///      sees the new instance.
///
/// After `kMaxRestartAttempts` failed restarts the slot is permanently
/// bypassed: `pumpSlot` returns false forever, the instance is dropped,
/// and an `Error / PluginEvent` is posted to the notification sink.
///
/// **No production consumer in S3/S4.** MainComponent is unchanged this
/// session; the integration test in
/// `tests/OutputMixerPluginHostIntegrationTests.cpp` exercises the chain
/// path, and the new supervisor tests in
/// `tests/OutOfProcessEffectChainHostSupervisorTests.cpp` exercise the
/// restart cycle. MainComponent wiring lands when the plug-in-adding UI
/// is real (post-M7).
///
/// Threading contract (same shape as the M5/M6 collaborator pattern —
/// e.g. `OutputMixer::setBusEffectChain`):
///   - `configureBus(...)`, `setNotificationSink(...)`, and the destructor
///     are **message-thread** only. They may allocate, spawn / reap child
///     processes, mmap / munmap shm.
///   - `pumpSlot(...)` is **audio-thread** only. Wait-free, allocation-
///     free, lock-free, bounded per `docs/RT_SAFETY_CONTRACT.md §6`.
///   - The supervisor thread is owned by this class. It runs from the
///     constructor through the destructor; the destructor signals exit
///     and joins BEFORE destroying any `SlotState`.
///   - The `instances_` map is mutated by `configureBus` (message thread,
///     before audio starts) AND by the supervisor thread (under
///     `instancesMutex_`). The audio thread reads through `instances_`
///     without taking the mutex — see the read-side ordering notes on
///     `pumpSlot` below.
class OutOfProcessEffectChainHost : public IEffectChainHost
{
public:
    /// Per-slot consecutive-miss count that triggers a supervisor restart.
    /// At a 64-sample / 44.1 kHz buffer (~1.45 ms) this is ~23 ms of dry
    /// output — well below the human perceptible "the plug-in stalled"
    /// threshold and well above the natural one-or-two-buffer transients
    /// the SPSC ring rides out without help.
    static constexpr std::uint32_t kConsecutiveMissThreshold = 16;

    /// Maximum number of restart attempts before the slot is permanently
    /// bypassed. After this many failures the slot's instance is destroyed
    /// and `pumpSlot` returns false forever — the chain has decided this
    /// plug-in is irrecoverably broken.
    static constexpr std::uint32_t kMaxRestartAttempts = 3;

    /// Supervisor polling cadence in milliseconds. The supervisor wakes
    /// every `kSupervisorPollMs` to inspect each slot's miss counter.
    /// Faster polling means faster restart latency at the cost of more
    /// wake-ups; 50 ms is the V7 §9.1 acceptance window.
    static constexpr int kSupervisorPollMs = 50;

    /// Bypass-flag-fence quiescence window. After setting `bypassed = true`
    /// the supervisor waits this many milliseconds before tearing down the
    /// old instance, guaranteeing any in-flight audio-thread `pumpSlot`
    /// call finished on the old instance. Audio buffers are typically
    /// 1..12 ms; `pumpSlot` itself runs in under 1 ms; 100 ms is two
    /// orders of magnitude of headroom.
    static constexpr int kRestartGraceMs = 100;

    OutOfProcessEffectChainHost();
    ~OutOfProcessEffectChainHost() override;

    OutOfProcessEffectChainHost (const OutOfProcessEffectChainHost&) = delete;
    OutOfProcessEffectChainHost& operator= (const OutOfProcessEffectChainHost&) = delete;
    OutOfProcessEffectChainHost (OutOfProcessEffectChainHost&&) = delete;
    OutOfProcessEffectChainHost& operator= (OutOfProcessEffectChainHost&&) = delete;

    /// Message-thread setter. Binds a notification sink for the supervisor
    /// to post `PluginEvent` messages into on restart + permanent-bypass.
    /// Set-once before audio starts; passing nullptr is legal (the
    /// supervisor checks before posting, so tests that don't care about
    /// notifications can leave it unbound).
    void setNotificationSink (INotificationSink* sink) noexcept;

    /// Message-thread setter. Spawns one `OutOfProcessPluginInstance` per
    /// non-bypassed slot of `chain` in CLAP mode against `clapBundle`,
    /// keyed by `(busId, slotIndex)`. Any pre-existing instances for the
    /// same `busId` that no longer have a corresponding slot are torn down.
    ///
    /// S3 simplification (retained in S4): every non-bypassed slot in the
    /// chain spawns against the SAME `clapBundle` argument — the
    /// integration tests use a single synthetic identity plug-in. Real
    /// per-slot bundle selection lands when the scanner surfaces bundle
    /// paths on `PluginDescriptor` (post-M7 per V7 plan line 533+).
    void configureBus (std::int64_t       busId,
                       const EffectChain& chain,
                       const juce::File&  hostBinary,
                       const juce::File&  clapBundle);

    /// Audio-thread dispatch — see `IEffectChainHost::pumpSlot`. This
    /// concrete override packages the supplied stereo float planes into
    /// the CLAP-mode wire format (`uint32_t frameCount` + interleaved
    /// stereo floats) and ships them through the slot's
    /// `OutOfProcessPluginInstance::tryWriteBytes` /
    /// `tryReadBytes` pair. Returns true if a response was popped this
    /// buffer; false on a miss (caller leaves `outChannels` unchanged).
    ///
    /// S4 short-circuits: if the slot's `permanentlyBypassed` OR `bypassed`
    /// atomic is set (acquire-loaded), `pumpSlot` returns false
    /// immediately without touching the instance — the bypass-flag fence
    /// the supervisor uses to swap instances safely.
    ///
    /// Pipelined 1-buffer delay: the push for buffer N and the pop of
    /// the response for buffer N-1 happen in the same call. The first
    /// call after `configureBus` always returns false (no prior response
    /// to pop); subsequent calls return true at steady state.
    bool pumpSlot (std::int64_t        busId,
                   std::size_t         slotIndex,
                   const float* const* inChannels,
                   float* const*       outChannels,
                   int                 numChannels,
                   int                 numSamples) noexcept override;

    // ---- Internal-FX dispatch (P7 T3a-B) — message-thread only ----------
    //
    // Binds (or unbinds) one of IDA's built-in FX adapters at a
    // `(nodeKey, slotIdx)` pair. The audio thread's `pumpSlot` checks the
    // internal-FX table BEFORE the OOP plugin path on every call; a hit
    // routes through the adapter and short-circuits the OOP child entirely.
    //
    // Precondition (same shape as `setEffectChainHost` and `configureBus`):
    // the caller MUST detach the audio callback before invoking. The
    // internal-FX table is read by the audio thread without a lock — see
    // the ordering notes in the SlotState / `pumpSlot` docblocks. Mutations
    // only happen on the message thread with the audio callback detached,
    // so the RT-side `find()` is race-free.
    //
    // `id == std::nullopt`: erase any existing adapter at `(nodeKey,
    // slotIdx)`.
    // `id == value`:        construct a fresh adapter via
    //                       `makeInternalFxAdapter(*id)` and emplace-or-
    //                       replace at `(nodeKey, slotIdx)`. If the factory
    //                       returns `nullptr` (an `InternalFxId` whose
    //                       T3 sub-task hasn't shipped yet — e.g. `kCmp`
    //                       before T3b), the existing entry is erased and
    //                       no new adapter is stored (effective no-op
    //                       rebind that leaves the slot un-adapted; the
    //                       audio thread's `pumpSlot` will then fall
    //                       through to the OOP path on miss as it always
    //                       has).
    //
    // If `prepareInternalFx(...)` has already been called on the host with a
    // (sampleRate, maxBlockSize) pair, the newly-constructed adapter is
    // immediately prepared with those values so its first `process` call
    // returns true rather than the unprepared-miss `false`.
    void setInternalFxAtSlot (std::int64_t                nodeKey,
                              std::size_t                 slotIdx,
                              std::optional<InternalFxId> id) override;

    // P7 T5 slice 1 — message-thread bypass toggle. Same precondition as
    // `setInternalFxAtSlot`: the caller MUST detach the audio callback
    // before invoking (atomic store is release-ordered to pair with the
    // audio thread's acquire-load in `pumpSlot`, but the underlying
    // map's bucket layout could still rehash if a brand-new key is
    // inserted, so we keep the established "mutate only when detached"
    // contract rather than rely on bucket stability).
    //
    // The flag is stored independently of `setInternalFxAtSlot` —
    // calling on a key with no bound adapter records the flag for any
    // future bind, but has no audio-thread effect until an adapter is
    // installed (the `pumpSlot` bypass check only runs after the
    // adapter lookup succeeds). `setInternalFxAtSlot(...)` with a
    // non-null id resets the matching bypass entry to false so a
    // re-added slot starts non-bypassed regardless of prior state.
    void setInternalFxBypassAtSlot (std::int64_t nodeKey,
                                    std::size_t  slotIdx,
                                    bool         bypassed) override;

    // P7 T5 slice 2 — message-thread reorder. Same precondition as
    // `setInternalFxAtSlot`: caller MUST detach the audio callback before
    // invoking. Swaps `internalAdapters_` AND `internalBypass_` entries
    // between `(nodeKey, fromSlot)` and `(nodeKey, toSlot)` so the bypass
    // flag travels with its adapter. Same key uses the same node — moves
    // across nodes are not in scope (a future API can add that if cross-
    // strip drag-reorder lands; today's UI doesn't need it).
    void moveInternalFxSlot (std::int64_t nodeKey,
                             std::size_t  fromSlot,
                             std::size_t  toSlot) override;

    // Slice EC — typed config setters/getters. Message-thread only;
    // caller MUST detach the audio callback before invoking the setters
    // (matches `setInternalFxAtSlot`). The getters take a snapshot of
    // the live config under the same precondition. All four no-op
    // silently when the slot is empty or holds the wrong kind of
    // adapter (the no-op default lives on `IInternalFxAdapter` so the
    // dispatch is virtual, not dynamic_cast).
    void setInternalEqConfigAt  (std::int64_t nodeKey,
                                 std::size_t  slotIdx,
                                 const EqConfig& cfg) override;
    std::optional<EqConfig>
         internalEqConfigAt     (std::int64_t nodeKey,
                                 std::size_t  slotIdx) const noexcept override;
    void setInternalCmpConfigAt (std::int64_t nodeKey,
                                 std::size_t  slotIdx,
                                 const CmpConfig& cfg) override;
    std::optional<CmpConfig>
         internalCmpConfigAt    (std::int64_t nodeKey,
                                 std::size_t  slotIdx) const noexcept override;

    // Message-thread only. Forwards `prepare(sampleRate, maxBlockSize)` to
    // every currently-bound internal-FX adapter and remembers the values
    // so adapters bound later by `setInternalFxAtSlot` are auto-prepared
    // against the same configuration.
    //
    // Named `prepareInternalFx` (not bare `prepare`) so call sites don't
    // collide-by-name with `OutOfProcessPluginInstance::prepare(...)` — both
    // types appear in the same translation units and a plain `prepare` is
    // ambiguous-by-eye. Mirrors `setInternalFxAtSlot`'s naming.
    //
    // Call once after the audio device is configured and again whenever
    // sample-rate / max-block changes (the engine already has the hooks
    // for this — wiring those hooks into the live audio device is
    // Subagent C's scope, not T3a-B).
    void prepareInternalFx (double sampleRate, int maxBlockSize) override;

    // ---- Editor wire-through (M7 S5) — message-thread only --------------
    //
    // Thin slot-keyed forwarders onto `OutOfProcessPluginInstance`'s editor
    // API. Take `instancesMutex_` (message-thread, never contended on the
    // audio side). Return false / 0 if the slot is unconfigured,
    // bypassed, or permanently bypassed. The supervisor's `attemptRestart`
    // observes the recorded editor intent (`editorWasShowing_` +
    // `editorWidth_` / `editorHeight_` in SlotState) and re-issues the
    // Show against the freshly-spawned instance.

    bool requestEditorShow   (std::int64_t busId, std::size_t slotIndex,
                              std::uint32_t width, std::uint32_t height) noexcept;
    bool requestEditorHide   (std::int64_t busId, std::size_t slotIndex) noexcept;
    bool requestEditorResize (std::int64_t busId, std::size_t slotIndex,
                              std::uint32_t width, std::uint32_t height) noexcept;
    std::uint32_t editorCaContextId (std::int64_t busId,
                                     std::size_t slotIndex) const noexcept;
    std::pair<std::uint32_t,std::uint32_t>
                  editorSize        (std::int64_t busId,
                                     std::size_t slotIndex) const noexcept;

    // ---- State + descriptor query (M8 S2) — message-thread only --------
    //
    // Used by the engine save / load path (SessionSnapshot) to populate
    // VersionPinningRecords against the live plug-in's actual identity
    // and state, not just the cached entry.descriptor + empty blob.

    /// Returns the live descriptor for the slot, or nullopt if the slot
    /// is unconfigured, bypassed, or permanently bypassed. Takes
    /// `instancesMutex_`.
    std::optional<PluginDescriptor> descriptorForSlot (
        std::int64_t busId, std::size_t slotIndex) const noexcept;

    /// Asks the slot's plug-in to save its state through the state shm
    /// region. Returns the bytes on success, nullopt on timeout, plug-in
    /// error, or absent slot. Posts a Warning / PluginEvent notification
    /// on timeout (so the operator sees the pinning fall-back).
    ///
    /// Snapshots the slot's `shared_ptr<SlotState>` under `instancesMutex_`
    /// and then RELEASES the mutex before the bounded (250 ms) IPC wait —
    /// the lock is never held across the blocking round-trip, so a wedged
    /// child cannot stall `configureBus` or the supervisor. The snapshot
    /// keeps the SlotState alive across the unlocked window. 250 ms matches
    /// the `OutOfProcessPluginInstance::requestStateSave` call here.
    std::optional<std::vector<std::byte>> stateBlobForSlot (
        std::int64_t busId, std::size_t slotIndex) const noexcept;

    /// Test-only accessor — returns the supervisor restart count for the
    /// given slot, or 0 if no slot is registered. Used by the supervisor
    /// tests to verify that restart escalation actually happened without
    /// having to scrape NotificationBus messages. Message-thread only.
    std::uint32_t restartCountForTesting (std::int64_t busId,
                                          std::size_t  slotIndex) const noexcept;

    /// Test-only accessor — returns true if the given slot has been
    /// permanently bypassed (3 consecutive restart failures).
    bool permanentlyBypassedForTesting (std::int64_t busId,
                                        std::size_t  slotIndex) const noexcept;

    /// Test-only accessor — returns the POSIX pid of the slot's current
    /// `OutOfProcessPluginInstance`, or -1 if no slot exists / no instance
    /// is currently spawned (e.g. mid-restart or permanently bypassed).
    /// Lets the supervisor tests SIGKILL successive child generations to
    /// drive the restart escalation cycle.
    long childPidForTestingAtSlot (std::int64_t busId,
                                   std::size_t  slotIndex) const noexcept;

    /// Test-only seam — binds a caller-constructed adapter at
    /// `(nodeKey, slotIdx)`, bypassing the `InternalFxFactory`. The host
    /// takes ownership and (if `prepareInternalFx` has already run) calls
    /// `adapter->prepare(...)` against the stored sample-rate / max-block.
    /// Used by tests that need a counting / observing mock adapter — e.g.
    /// the T3a I-1 idempotency test that verifies a redundant
    /// `prepareInternalFx(sr, maxBlock)` call does NOT re-invoke
    /// `adapter->prepare()`. Message-thread only with the audio callback
    /// detached, same precondition as the production `setInternalFxAtSlot`.
    void setInternalFxAdapterForTesting (
        std::int64_t                          nodeKey,
        std::size_t                           slotIdx,
        std::unique_ptr<IInternalFxAdapter>   adapter);

private:
    /// `(busId, slotIndex)` lookup key. Composed of trivially copyable
    /// integers so the `unordered_map` hashing stays allocation-free.
    using SlotKey = std::pair<std::int64_t, std::size_t>;

    struct SlotKeyHash
    {
        std::size_t operator() (const SlotKey& key) const noexcept
        {
            const auto a = static_cast<std::uint64_t> (key.first);
            const auto b = static_cast<std::uint64_t> (key.second);
            std::uint64_t h = a + 0x9e3779b97f4a7c15ULL + (b << 6) + (b >> 2);
            h ^= b;
            return static_cast<std::size_t> (h);
        }
    };

    /// Per-slot state — the heap-allocated unit that owns the plug-in
    /// instance plus the watchdog/supervisor bookkeeping. Held by
    /// `std::shared_ptr` in the map so the supervisor can snapshot a
    /// strong reference under `instancesMutex_` and then run its slow
    /// restart sequence (sleep + spawn) WITHOUT holding the mutex AND
    /// without risking use-after-free if `configureBus` erases the map
    /// entry concurrently — the snapshot's shared_ptr keeps the SlotState
    /// alive across the unlocked window even if the map drops it. The
    /// audio thread does NOT touch the shared_ptr refcount (see the
    /// `pumpSlot` ordering note); only the message thread (configureBus
    /// emplace/erase) and the supervisor thread (snapshot copy + drop)
    /// pay the atomic-refcount cost. Atomics live inside `SlotState`
    /// (non-moveable, which also rules out by-value map storage).
    ///
    /// **Atomics are accessed from two threads:** `pumpSlot` (audio thread)
    /// reads `bypassed` and `permanentlyBypassed` with acquire ordering
    /// and writes `consecutiveMisses` with relaxed ordering; the
    /// supervisor reads `consecutiveMisses` with relaxed ordering and
    /// writes `bypassed`/`permanentlyBypassed` with release ordering.
    /// Non-atomic fields (`restartCount`, `descriptor`, `hostBinary`,
    /// `clapBundle`, `instanceId`) are touched only by the supervisor
    /// (and `configureBus` at construction time, before the supervisor
    /// can see the slot).
    struct SlotState
    {
        std::unique_ptr<OutOfProcessPluginInstance> instance;
        std::atomic<std::uint32_t> consecutiveMisses { 0 };
        std::atomic<bool>          bypassed { false };
        std::atomic<bool>          permanentlyBypassed { false };
        std::uint32_t              restartCount { 0 };
        PluginDescriptor           descriptor;
        juce::File                 hostBinary;
        juce::File                 clapBundle;
        std::string                instanceId;

        // M7 S5 — editor intent persisted across supervisor restarts so
        // the supervisor can re-issue the Show against the new child.
        // Message-thread only (read/written from configureBus + the GUI
        // forwarders + attemptRestart under instancesMutex_); no atomics
        // needed.
        bool          editorWasShowing { false };
        std::uint32_t editorWidth      { 0 };
        std::uint32_t editorHeight     { 0 };
    };

    /// Audio-thread scratch for the interleaved CLAP wire bytes. Sized
    /// at construction to `PluginIpcMessage::kMaxPayloadBytes` — one
    /// max-size message. `mutable` so `pumpSlot` (which is `noexcept`
    /// and operates on internal state the audio thread owns end-to-end)
    /// can write to it without losing the `noexcept` shape.
    static constexpr int kPumpChannels = 2;

    /// Slot map. `shared_ptr<SlotState>` so the supervisor thread can
    /// snapshot strong references under `instancesMutex_` and outlive a
    /// concurrent `configureBus` erase — see the SlotState docblock for
    /// the use-after-free reasoning. The audio thread reads `it->second`
    /// as `const shared_ptr&` (no refcount bump) and dereferences via
    /// `.get()` — see the ordering notes on `pumpSlot`.
    std::unordered_map<SlotKey,
                       std::shared_ptr<SlotState>,
                       SlotKeyHash> instances_;

    /// P7 T3a-B — internal-FX adapter table, keyed by `(nodeKey, slotIdx)`.
    /// Mutations happen ONLY on the message thread with the audio callback
    /// detached (the same set-once-before-audio-attaches contract used by
    /// `configureBus` for the OOP `instances_` map). Therefore the audio
    /// thread's `pumpSlot` can `find()` into this map without taking a lock
    /// AND without facing a concurrent rehash — no reader/writer race.
    /// The audio thread reads `it->second.get()` as a raw pointer and never
    /// touches the `unique_ptr` itself.
    ///
    /// This map is INDEPENDENT of `instances_`: a `(nodeKey, slotIdx)` pair
    /// may have an internal adapter, an OOP plugin instance, both, or
    /// neither. `pumpSlot` checks `internalAdapters_` FIRST and falls
    /// through to `instances_` on miss — see the dispatch ordering comment
    /// at the top of `pumpSlot`.
    std::unordered_map<SlotKey,
                       std::unique_ptr<IInternalFxAdapter>,
                       SlotKeyHash> internalAdapters_;

    /// P7 T5 slice 1 — parallel bypass-flag table for the internal-FX
    /// dispatch path. Keyed identically to `internalAdapters_`; entries
    /// are only ever created or erased on the message thread with the
    /// audio callback detached (same contract as `internalAdapters_`).
    /// The audio thread's `pumpSlot` reads `it->second.load(acquire)`
    /// only after an adapter lookup hit, so a default-absent entry
    /// (no bypass ever set for this key) behaves identically to
    /// `bypassed == false`. `std::atomic<bool>` because the message
    /// thread writes it (release) while the audio thread reads it
    /// (acquire) on every pumpSlot call to a bound adapter.
    std::unordered_map<SlotKey,
                       std::atomic<bool>,
                       SlotKeyHash> internalBypass_;

    /// P7 T3a-B — last-known `prepareInternalFx(sampleRate, maxBlockSize)`
    /// values. `prepared_` flips to true on the first call to
    /// `prepareInternalFx(...)`. Used by `setInternalFxAtSlot` to
    /// auto-prepare freshly-constructed adapters against the live device
    /// configuration so their first `process` call returns true rather than
    /// the unprepared-miss false. Message-thread only — written by
    /// `prepareInternalFx(...)`, read by `setInternalFxAtSlot(...)`.
    double currentSampleRate_ { 0.0 };
    int    currentMaxBlock_   { 0 };
    bool   prepared_          { false };

    /// Pre-allocated scratch bytes for the wire-format packaging in
    /// pumpSlot. Sized to one PluginIpcMessage payload. Pre-allocated in
    /// the constructor; never resized.
    mutable std::vector<std::byte> wireScratch_;

    /// Notification sink for `PluginEvent` posts on restart and permanent
    /// bypass. Set-once via `setNotificationSink(...)` on the message
    /// thread; nullptr-tolerant (the supervisor checks before posting).
    /// Read by the supervisor thread; `std::atomic` so the load happens
    /// without taking the supervisor mutex.
    std::atomic<INotificationSink*> notificationSink_ { nullptr };

    /// Serializes `configureBus` and the supervisor's mutating reads of
    /// `instances_`. The audio thread does NOT take this mutex — see the
    /// `pumpSlot` doc + the `instances_` ordering contract in the class
    /// docblock.
    mutable std::mutex instancesMutex_;

    /// Supervisor thread state — pattern lifted from `TapeWriter`:
    /// `std::thread` member, started in ctor, joined in dtor under a
    /// `shouldExit` + condition_variable wake.
    std::thread                supervisorThread_;
    std::atomic<bool>          supervisorShouldExit_ { false };
    mutable std::mutex         supervisorWakeMutex_;
    std::condition_variable    supervisorWakeCv_;

    /// Supervisor loop body. Wakes every `kSupervisorPollMs` (or on
    /// shutdown), inspects every slot's miss counter, and triggers
    /// `attemptRestart` on threshold-hit slots that are still recoverable.
    void supervisorLoop();

    /// Performs the bypass-flag-fenced restart sequence on a single slot.
    /// On the third consecutive failure, sets `permanentlyBypassed` and
    /// destroys the instance. Posts `Info / PluginEvent` on successful
    /// restart and `Error / PluginEvent` on permanent bypass.
    void attemptRestart (SlotState& state);

    /// Helper — posts to `notificationSink_` if bound. nullptr-tolerant.
    void postNotification (NotificationLevel level, const char* message) noexcept;
};

} // namespace ida
