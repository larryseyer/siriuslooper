#include "sirius/OutOfProcessEffectChainHost.h"

#include "sirius/PluginInstanceId.h"
#include "sirius/PluginIpcMessage.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace sirius
{

namespace
{
    /// Builds the per-slot instance id from `(busId, slotIndex)`. Format:
    /// `bN_sM` where N and M are the integer values. The result is fed
    /// through `hashInstanceId` so any combination that would overflow
    /// the macOS shm name budget gets transparently hashed.
    std::string makeInstanceId (std::int64_t busId, std::size_t slotIndex)
    {
        std::string raw = "b" + std::to_string (busId)
                         + "_s" + std::to_string (slotIndex);
        return hashInstanceId (raw);
    }

    /// On supervisor restart we need a NEW shm segment name — the old
    /// one is shm_unlinked when the old instance's `SharedMemoryRegion`s
    /// destruct, but we don't want any chance of a stale segment lingering
    /// (e.g. if a previous crash test left one behind). Append the restart
    /// generation, then hash so the result still fits the macOS shm budget.
    std::string makeRestartInstanceId (const std::string& baseInstanceId,
                                       std::uint32_t restartAttempt)
    {
        std::string raw = baseInstanceId + "_r" + std::to_string (restartAttempt);
        return hashInstanceId (raw);
    }
}

OutOfProcessEffectChainHost::OutOfProcessEffectChainHost()
    : wireScratch_ (PluginIpcMessage::kMaxPayloadBytes)
{
    // Modest reservation to keep configureBus's emplace from rehashing
    // for typical small-chain populations.
    instances_.reserve (16);

    // Start the supervisor immediately so even slot-less hosts have a
    // running supervisor — keeps the lifecycle symmetric and avoids a
    // "first-configureBus starts the thread" race against the audio
    // thread's first pumpSlot.
    supervisorThread_ = std::thread (&OutOfProcessEffectChainHost::supervisorLoop, this);
}

OutOfProcessEffectChainHost::~OutOfProcessEffectChainHost()
{
    // Signal + join the supervisor BEFORE destroying instances_ — if
    // teardown order were reversed the supervisor's final poll could
    // touch freed SlotState memory. TapeWriter dtor uses the identical
    // shape (signal under wakeMutex_, notify, join).
    {
        std::scoped_lock lk (supervisorWakeMutex_);
        supervisorShouldExit_.store (true, std::memory_order_release);
    }
    supervisorWakeCv_.notify_all();
    if (supervisorThread_.joinable())
        supervisorThread_.join();

    // instances_ destructs here, taking each SlotState's instance with
    // it (which SIGTERMs + SIGKILLs + reaps the child cleanly).
}

void OutOfProcessEffectChainHost::setNotificationSink (INotificationSink* sink) noexcept
{
    notificationSink_.store (sink, std::memory_order_release);
}

void OutOfProcessEffectChainHost::configureBus (std::int64_t       busId,
                                                const EffectChain& chain,
                                                const juce::File&  hostBinary,
                                                const juce::File&  clapBundle)
{
    // M7 S9 — Reaper-style separate plug-in windows. The child process
    // owns its own top-level NSWindow; no cross-process pixel handoff,
    // no PluginGuiBridge touch needed here. The S5 PluginGuiState shm
    // contract (requestKind / responseContextId) still carries editor
    // show/hide signals; the child publishes responseContextId=1 when
    // its window is up, 0 when closed.
    std::scoped_lock lk (instancesMutex_);

    // Tear down any pre-existing instances for this bus that no longer
    // have a corresponding slot in the new chain. Walk first, collect
    // keys to remove, then erase — avoids invalidating iterators while
    // we're inspecting them.
    std::vector<SlotKey> staleKeys;
    staleKeys.reserve (instances_.size());
    for (const auto& kv : instances_)
    {
        if (kv.first.first != busId) continue; // different bus, leave alone
        const std::size_t slotIdx = kv.first.second;
        const bool slotPresent =
            slotIdx < chain.entries().size()
            && ! chain.entries()[slotIdx].bypassed;
        if (! slotPresent)
            staleKeys.push_back (kv.first);
    }
    for (const auto& key : staleKeys)
        instances_.erase (key);

    // Spawn a SlotState (and the underlying instance) for each non-bypassed
    // slot that doesn't already have one. Bypassed slots are not spawned
    // at all — the audio thread would skip them via pumpSlot's missing-
    // instance branch anyway, but not spawning is cheaper and matches the
    // "no resources for inactive slots" principle.
    const auto& entries = chain.entries();
    for (std::size_t slotIdx = 0; slotIdx < entries.size(); ++slotIdx)
    {
        if (entries[slotIdx].bypassed)
            continue;

        const SlotKey key { busId, slotIdx };
        if (instances_.find (key) != instances_.end())
            continue; // already have one

        // Double-bind would silently shadow the other path's dispatch; Subagent C's
        // caller is expected to maintain the per-key exclusivity invariant.
        jassert (internalAdapters_.find (key) == internalAdapters_.end());

        const std::string instanceId = makeInstanceId (busId, slotIdx);
        auto state = std::make_shared<SlotState>();
        state->descriptor = entries[slotIdx].descriptor;
        state->hostBinary = hostBinary;
        state->clapBundle = clapBundle;
        state->instanceId = instanceId;
        state->instance   = std::make_unique<OutOfProcessPluginInstance> (
            hostBinary, instanceId, clapBundle);

        instances_.emplace (key, std::move (state));
    }
}

void OutOfProcessEffectChainHost::setInternalFxAtSlot (
    std::int64_t                nodeKey,
    std::size_t                 slotIdx,
    std::optional<InternalFxId> id)
{
    // Message-thread only — precondition documented in the header. Caller
    // MUST have detached the audio callback before invoking, so we can
    // mutate `internalAdapters_` without contending with the audio-thread
    // `find()` in pumpSlot.
    //
    // Allocation is allowed here (adapter construction, unordered_map
    // bucket emplace, optional adapter->prepare).
    const SlotKey key { nodeKey, slotIdx };

    // No id → erase any existing adapter and we're done.
    if (! id.has_value())
    {
        internalAdapters_.erase (key);
        return;
    }

    // Factory returns nullptr for ids whose adapter sub-task hasn't shipped
    // yet (T3a: only kEq is implemented; kCmp/kRvb/kDly return nullptr).
    // Treat that as "no adapter for this slot" — erase any existing entry
    // so a previous (different-id) adapter doesn't linger, and bail without
    // storing a null.
    auto adapter = makeInternalFxAdapter (*id);
    if (adapter == nullptr)
    {
        internalAdapters_.erase (key);
        return;
    }

    // If the host has already been prepared with a (sampleRate, maxBlock)
    // pair, prepare the fresh adapter immediately so its first audio-thread
    // `process` call returns true rather than the unprepared-miss `false`.
    // If `prepareInternalFx(...)` hasn't been called yet, the adapter is
    // stored unprepared; the next `prepareInternalFx(...)` call will sweep it.
    if (prepared_)
        adapter->prepare (currentSampleRate_, currentMaxBlock_);

    // Double-bind would silently shadow the other path's dispatch; Subagent C's
    // caller is expected to maintain the per-key exclusivity invariant.
    jassert (instances_.find (key) == instances_.end());

    // emplace_or_replace: assigning into the unique_ptr destroys any
    // pre-existing adapter at this key (the old one's destructor runs
    // on the message thread, never touching the audio path).
    internalAdapters_[key] = std::move (adapter);
}

void OutOfProcessEffectChainHost::prepareInternalFx (double sampleRate, int maxBlockSize)
{
    // Message-thread only — see the header. Forwards to every currently-
    // bound adapter and stashes the values so subsequent
    // `setInternalFxAtSlot(...)` calls can auto-prepare new adapters
    // against the same configuration.
    //
    // P7 T3a I-1 — idempotency early-return. `rebuildInputStrips()` fires
    // on every stereo-toggle and device-config rebuild, not just on
    // sample-rate change; each call funnels through here. Re-walking the
    // adapter table and re-invoking `adapter->prepare(...)` re-runs each
    // adapter's coefficient computation AND clears IIR state mid-buffer
    // (PlayerEQ::ProcessorDuplicator semantics). Short-circuit when
    // nothing has actually changed so a bound EQ keeps its filter history
    // across stereo-toggles. The first call (`prepared_ == false`) always
    // proceeds.
    if (prepared_
        && sampleRate   == currentSampleRate_
        && maxBlockSize == currentMaxBlock_)
        return;

    currentSampleRate_ = sampleRate;
    currentMaxBlock_   = maxBlockSize;
    prepared_          = true;

    for (auto& [key, adapter] : internalAdapters_)
    {
        if (adapter != nullptr)
            adapter->prepare (sampleRate, maxBlockSize);
    }
}

void OutOfProcessEffectChainHost::setInternalFxAdapterForTesting (
    std::int64_t                          nodeKey,
    std::size_t                           slotIdx,
    std::unique_ptr<IInternalFxAdapter>   adapter)
{
    // Test-only seam — same message-thread + detached-callback contract as
    // `setInternalFxAtSlot`. Bypasses the factory so tests can inject a
    // mock that records invocations (e.g. T3a I-1 idempotency counter).
    const SlotKey key { nodeKey, slotIdx };

    if (adapter == nullptr)
    {
        internalAdapters_.erase (key);
        return;
    }

    if (prepared_)
        adapter->prepare (currentSampleRate_, currentMaxBlock_);

    internalAdapters_[key] = std::move (adapter);
}

bool OutOfProcessEffectChainHost::pumpSlot (std::int64_t        busId,
                                            std::size_t         slotIndex,
                                            const float* const* inChannels,
                                            float* const*       outChannels,
                                            int                 numChannels,
                                            int                 numSamples) noexcept
{
    // Argument validation first — defensive guards never throw, never
    // allocate, and preserve the noexcept contract.
    if (inChannels == nullptr || outChannels == nullptr) return false;
    if (numChannels <= 0 || numSamples <= 0)             return false;

    const int channels = std::min (numChannels, kPumpChannels);
    if (inChannels[0] == nullptr) return false;
    if (channels >= 2 && inChannels[1] == nullptr) return false;

    // P7 T3a-B — dispatch order: internal-FX table FIRST, OOP plugin path
    // on miss. The internal table is mutated only on the message thread
    // with the audio callback detached (see the storage docblock in
    // OutOfProcessEffectChainHost.h), so this `find()` is race-free and
    // alloc-free (`std::unordered_map::find` never rehashes). See the
    // architecture decision in
    // ~/.claude/plans/read-continue-and-proceed-structured-acorn.md for
    // why the internal adapter lives inside this host rather than in a
    // composite wrapper above it.
    {
        const SlotKey internalKey { busId, slotIndex };
        const auto adapterIt = internalAdapters_.find (internalKey);
        if (adapterIt != internalAdapters_.end() && adapterIt->second != nullptr)
        {
            // Internal adapter handles the slot. Its `process` returns
            // false on the unprepared-or-malformed-input miss path, which
            // matches `IEffectChainHost::pumpSlot`'s contract (leave
            // outChannels unmodified, caller treats as dry passthrough).
            return adapterIt->second->process (inChannels, outChannels,
                                               numChannels, numSamples);
        }
    }

    // Lookup the slot's state. The map's key set is mutated by
    // `configureBus` (message thread) under `instancesMutex_`; the audio
    // thread does NOT take that mutex. The set-once collaborator contract
    // (M5/M6, restated in the S4 header) is that `configureBus` runs to
    // completion BEFORE the audio thread first calls `pumpSlot`, so the
    // bucket layout is stable for any key the audio thread observes.
    //
    // S4 ownership model: map values are `shared_ptr<SlotState>`. We
    // dereference via `it->second.get()` to obtain a raw `SlotState*` —
    // NO `shared_ptr` copy, NO atomic refcount bump on the audio path.
    // The refcount is only touched on the message thread (configureBus
    // emplace/erase) and on the supervisor thread (snapshot copy + drop).
    // The audio thread observes a stable raw pointer because the bucket
    // is not erased while audio is running. (And under shared ownership,
    // even if it WERE erased, the supervisor's snapshot would keep the
    // SlotState alive — which closes the use-after-free hole that a
    // unique_ptr layout left open across the supervisor's unlocked
    // grace/spawn window.)
    //
    // The supervisor mutates `state->instance` IN PLACE under the
    // `bypassed`-flag fence — the SlotState itself stays put. This is
    // the dependency-inverted version of the M6 §2 atomic-snapshot
    // pattern, specialized to the case where the heap object stays put
    // but its members move.
    const SlotKey key { busId, slotIndex };
    const auto it = instances_.find (key);
    if (it == instances_.end() || it->second == nullptr)
        return false; // no instance for this slot — dry-on-miss

    auto* const statePtr = it->second.get(); // raw — no refcount work
    auto& state = *statePtr;

    // Bypass-flag-fence short-circuit. permanentlyBypassed wins over
    // bypassed because once the supervisor flips it the slot is dead
    // forever and we want zero cost on the audio thread. Both loads are
    // acquire so we synchronize-with the supervisor's release stores.
    if (state.permanentlyBypassed.load (std::memory_order_acquire))
        return false;
    if (state.bypassed.load (std::memory_order_acquire))
        return false;

    if (state.instance == nullptr)
        return false; // mid-restart window — supervisor cleared it

    auto& instance = *state.instance;

    // Package the input buffer into the CLAP-mode wire format the
    // sirius_plugin_host child expects (matches host_process/main.cpp
    // `runClapMode`): `uint32_t frameCount` followed by `frameCount ×
    // kPumpChannels × float` interleaved samples.
    constexpr std::size_t kHeaderBytes  = sizeof (std::uint32_t);
    const std::size_t     interleavedBytes =
        static_cast<std::size_t> (numSamples) * kPumpChannels * sizeof (float);
    const std::size_t     totalBytes  = kHeaderBytes + interleavedBytes;
    if (totalBytes > PluginIpcMessage::kMaxPayloadBytes)
    {
        // Oversized buffer — counts as a miss for the watchdog (the
        // operator should size their buffer to fit).
        state.consecutiveMisses.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    // Write the header (frameCount).
    const std::uint32_t frameCount = static_cast<std::uint32_t> (numSamples);
    std::memcpy (wireScratch_.data(), &frameCount, kHeaderBytes);

    // Interleave the input channels into the wire format. Mono inputs
    // are duplicated to L and R.
    float* const interleaved =
        reinterpret_cast<float*> (wireScratch_.data() + kHeaderBytes);
    if (channels >= 2)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            interleaved[s * kPumpChannels + 0] = inChannels[0][s];
            interleaved[s * kPumpChannels + 1] = inChannels[1][s];
        }
    }
    else
    {
        for (int s = 0; s < numSamples; ++s)
        {
            interleaved[s * kPumpChannels + 0] = inChannels[0][s];
            interleaved[s * kPumpChannels + 1] = inChannels[0][s];
        }
    }

    // Push this buffer onto the engine→host ring. Push-failure (full
    // ring) counts as a miss — that's the supervisor's signal that the
    // host child is not draining.
    const bool pushed = instance.tryWriteBytes (wireScratch_.data(), totalBytes);
    if (! pushed)
    {
        state.consecutiveMisses.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    // Pop the response for the PREVIOUS buffer (pipelined 1-buffer
    // delay). The host's response format is the raw interleaved float
    // payload (no `uint32_t frameCount` header — see host_process/main.cpp
    // :438-444).
    std::size_t bytesRead = 0;
    const bool popped = instance.tryReadBytes (
        wireScratch_.data(), wireScratch_.size(), bytesRead);
    if (! popped || bytesRead == 0)
    {
        // The first call after configureBus always sees an empty pop
        // (nothing pushed yet); subsequent empty pops mean the host is
        // not producing. Both count as misses — the supervisor disambiguates
        // by waiting for the threshold (16 buffers ≈ 23 ms).
        state.consecutiveMisses.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    // De-interleave back into the caller's output planes. The host
    // writes exactly `frameCount × kPumpChannels × float` bytes per
    // pumped buffer; if the response carries fewer frames than the
    // current buffer asked for, treat the shortfall as a miss for the
    // remaining samples (leave them untouched in outChannels).
    const std::size_t payloadFloats = bytesRead / sizeof (float);
    const std::size_t framesAvailable =
        std::min<std::size_t> (payloadFloats / kPumpChannels,
                               static_cast<std::size_t> (numSamples));

    const float* const respInterleaved =
        reinterpret_cast<const float*> (wireScratch_.data());
    if (outChannels[0] != nullptr)
    {
        for (std::size_t s = 0; s < framesAvailable; ++s)
            outChannels[0][s] = respInterleaved[s * kPumpChannels + 0];
    }
    if (channels >= 2 && outChannels[1] != nullptr)
    {
        for (std::size_t s = 0; s < framesAvailable; ++s)
            outChannels[1][s] = respInterleaved[s * kPumpChannels + 1];
    }

    if (framesAvailable > 0)
    {
        // Successful round-trip — reset the watchdog counter so transient
        // missed buffers don't accumulate across restart-free runs.
        state.consecutiveMisses.store (0, std::memory_order_relaxed);
        return true;
    }

    // Zero frames in the response payload — count as a miss.
    state.consecutiveMisses.fetch_add (1, std::memory_order_relaxed);
    return false;
}

// ---- Editor wire-through (M7 S5) -----------------------------------------

bool OutOfProcessEffectChainHost::requestEditorShow (std::int64_t busId,
                                                     std::size_t slotIndex,
                                                     std::uint32_t width,
                                                     std::uint32_t height) noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr)
        return false;
    auto& state = *it->second;
    if (state.permanentlyBypassed.load (std::memory_order_acquire))
        return false;

    state.editorWasShowing = true;
    state.editorWidth      = width;
    state.editorHeight     = height;

    if (state.instance == nullptr)
        return false; // mid-restart — supervisor will re-issue on respawn
    return state.instance->requestEditorShow (width, height);
}

bool OutOfProcessEffectChainHost::requestEditorHide (std::int64_t busId,
                                                     std::size_t slotIndex) noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr)
        return false;
    auto& state = *it->second;

    state.editorWasShowing = false;

    if (state.instance == nullptr)
        return false;
    return state.instance->requestEditorHide();
}

bool OutOfProcessEffectChainHost::requestEditorResize (std::int64_t busId,
                                                       std::size_t slotIndex,
                                                       std::uint32_t width,
                                                       std::uint32_t height) noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr)
        return false;
    auto& state = *it->second;
    if (state.permanentlyBypassed.load (std::memory_order_acquire))
        return false;

    state.editorWidth  = width;
    state.editorHeight = height;

    if (state.instance == nullptr)
        return false;
    return state.instance->requestEditorResize (width, height);
}

std::uint32_t OutOfProcessEffectChainHost::editorCaContextId (
    std::int64_t busId, std::size_t slotIndex) const noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr || it->second->instance == nullptr)
        return 0;
    return it->second->instance->editorCaContextId();
}

std::pair<std::uint32_t, std::uint32_t>
OutOfProcessEffectChainHost::editorSize (std::int64_t busId,
                                         std::size_t slotIndex) const noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr || it->second->instance == nullptr)
        return { 0, 0 };
    return it->second->instance->editorSize();
}

std::uint32_t OutOfProcessEffectChainHost::restartCountForTesting (
    std::int64_t busId, std::size_t slotIndex) const noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr)
        return 0;
    return it->second->restartCount;
}

bool OutOfProcessEffectChainHost::permanentlyBypassedForTesting (
    std::int64_t busId, std::size_t slotIndex) const noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr)
        return false;
    return it->second->permanentlyBypassed.load (std::memory_order_acquire);
}

long OutOfProcessEffectChainHost::childPidForTestingAtSlot (
    std::int64_t busId, std::size_t slotIndex) const noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const auto it = instances_.find ({ busId, slotIndex });
    if (it == instances_.end() || it->second == nullptr)
        return -1;
    if (it->second->instance == nullptr)
        return -1;
    return it->second->instance->childPidForTesting();
}

void OutOfProcessEffectChainHost::postNotification (NotificationLevel level,
                                                    const char* message) noexcept
{
    auto* const sink = notificationSink_.load (std::memory_order_acquire);
    if (sink == nullptr) return;
    sink->post (level, Category::PluginEvent, message);
}

void OutOfProcessEffectChainHost::supervisorLoop()
{
    while (! supervisorShouldExit_.load (std::memory_order_acquire))
    {
        // Wait kSupervisorPollMs OR until shutdown signals.
        {
            std::unique_lock lk (supervisorWakeMutex_);
            supervisorWakeCv_.wait_for (
                lk, std::chrono::milliseconds (kSupervisorPollMs),
                [this] { return supervisorShouldExit_.load (std::memory_order_acquire); });
        }

        if (supervisorShouldExit_.load (std::memory_order_acquire))
            break;

        // Snapshot the list of slots needing restart while holding the
        // mutex briefly. We do NOT do the (slow) restart work under the
        // mutex — that would block configureBus and the test-only
        // accessors for ~kRestartGraceMs + child reap latency.
        //
        // S4 (B1 fix): the snapshot copies `shared_ptr<SlotState>` values
        // (not raw pointers). If `configureBus` erases a map entry while
        // we're sleeping through the grace window or spawning a new child,
        // our local shared_ptr keeps the SlotState alive for the duration
        // of `attemptRestart`. When `needsRestart` goes out of scope at
        // the bottom of this loop iteration, the refcount drops and any
        // already-erased SlotState destructs cleanly.
        std::vector<std::shared_ptr<SlotState>> needsRestart;
        {
            std::scoped_lock lk (instancesMutex_);
            for (auto& [key, statePtr] : instances_)
            {
                if (statePtr == nullptr) continue;
                auto& state = *statePtr;
                if (state.permanentlyBypassed.load (std::memory_order_acquire))
                    continue;
                if (state.bypassed.load (std::memory_order_acquire))
                    continue; // already being restarted (shouldn't happen, but defensive)

                const auto misses = state.consecutiveMisses.load (std::memory_order_relaxed);
                if (misses >= kConsecutiveMissThreshold)
                    needsRestart.push_back (statePtr); // shared_ptr copy
            }
        }

        for (auto& state : needsRestart)
            attemptRestart (*state);
    }
}

void OutOfProcessEffectChainHost::attemptRestart (SlotState& state)
{
    // Step 1: raise the bypass flag. The audio thread acquire-loads this
    // on its NEXT pumpSlot call and returns false without touching the
    // (about-to-be-destroyed) instance.
    state.bypassed.store (true, std::memory_order_release);

    // Step 2: wait for the grace window so any pumpSlot already past the
    // bypass check has time to finish. pumpSlot runs in << 1 ms; audio
    // buffers are < 12 ms; kRestartGraceMs = 100 ms is two orders of
    // magnitude of headroom.
    std::this_thread::sleep_for (std::chrono::milliseconds (kRestartGraceMs));

    // Step 3: tear down the old instance. Its destructor SIGTERMs +
    // SIGKILLs + reaps the child and shm_unlinks the regions.
    state.instance.reset();

    // Step 4: check whether we're allowed another attempt. B2 fix:
    // gate on `restartCount >= kMaxRestartAttempts` BEFORE incrementing,
    // so kMaxRestartAttempts is the TOTAL number of restart attempts the
    // supervisor will make (numbered 1..N). With kMaxRestartAttempts = 3:
    // attempts 1, 2, 3 each spawn a replacement and post an Info; the
    // 4th supervisor entry (after the 3rd respawned child also fails)
    // hits this gate and posts the permanent-bypass Error instead. The
    // header docstring promises "the maximum number of restart attempts
    // before the slot is permanently bypassed" — exactly N attempts,
    // then escalate. The pre-fix `>` after-increment ordering allowed
    // a phantom 4th attempt that wasn't covered by the docstring.
    if (state.restartCount >= kMaxRestartAttempts)
    {
        // Permanent bypass. The instance is already gone; flag the
        // slot dead-forever and notify the operator.
        state.permanentlyBypassed.store (true, std::memory_order_release);

        // The bypass flag stays raised — both gates would short-circuit
        // pumpSlot anyway, but leaving it raised makes the slot's state
        // unambiguous to any future test-only inspection.

        char msg[128];
        std::snprintf (msg, sizeof (msg),
                       "%s permanently bypassed after %u restart attempts",
                       state.instanceId.c_str(),
                       static_cast<unsigned> (kMaxRestartAttempts));
        postNotification (NotificationLevel::Error, msg);
        return;
    }

    // Step 5: this is a real restart attempt; count it now (before the
    // spawn) so a throwing spawn still consumes one of the N attempts
    // and the next supervisor entry sees the updated count.
    state.restartCount += 1;

    // Spawn a replacement against the same descriptor + paths. The new
    // instanceId appends `_rN` (restart generation) so the new shm
    // segment name cannot collide with a stale one from a previous
    // crashed instance.
    //
    // S2 fix: `make_unique<OutOfProcessPluginInstance>` can throw
    // (shm exhaustion, missing binary on disk, posix_spawn ENOMEM, …).
    // The supervisor thread MUST NOT let an exception unwind out of
    // `attemptRestart` — that would `std::terminate` the host process.
    // On catch we leave the slot bypassed (no instance to pump), post
    // a Warning to the notification sink, and let the NEXT supervisor
    // poll re-enter and either retry-or-escalate-to-permanent-bypass
    // based on `restartCount` (incremented above, so this failed attempt
    // counts towards the kMaxRestartAttempts budget).
    const auto newInstanceId = makeRestartInstanceId (state.instanceId,
                                                      state.restartCount);

    try
    {
        state.instance = std::make_unique<OutOfProcessPluginInstance> (
            state.hostBinary, newInstanceId, state.clapBundle);
    }
    catch (const std::exception& e)
    {
        state.instance.reset();
        // Leave state.bypassed == true — there is no instance to pump,
        // and pumpSlot's bypass-fence already short-circuits to dry.

        char failMsg[128];
        std::snprintf (failMsg, sizeof (failMsg),
                       "%s restart attempt %u failed: %s",
                       state.instanceId.c_str(),
                       static_cast<unsigned> (state.restartCount),
                       e.what());
        postNotification (NotificationLevel::Warning, failMsg);
        return;
    }
    catch (...)
    {
        state.instance.reset();
        char failMsg[128];
        std::snprintf (failMsg, sizeof (failMsg),
                       "%s restart attempt %u failed: unknown exception",
                       state.instanceId.c_str(),
                       static_cast<unsigned> (state.restartCount));
        postNotification (NotificationLevel::Warning, failMsg);
        return;
    }

    // Reset the watchdog counter. The next pumpSlot will start fresh
    // against the new child.
    state.consecutiveMisses.store (0, std::memory_order_relaxed);

    // M7 S5 — re-publish the editor against the freshly-spawned child
    // BEFORE lowering the bypass flag. The new instance's GUI region
    // starts with requestSeq=0 / responseSeq=0; if the operator had the
    // editor open at the time of the restart, re-issue Show so the
    // parent UI's polling loop picks up the new CAContextID. Safe under
    // the bypass flag because pumpSlot is still short-circuiting.
    if (state.editorWasShowing && state.instance != nullptr
        && state.editorWidth > 0 && state.editorHeight > 0)
    {
        state.instance->requestEditorShow (state.editorWidth, state.editorHeight);
    }

    // Lower the bypass flag. The next pumpSlot call sees the new instance
    // and resumes pumping.
    state.bypassed.store (false, std::memory_order_release);

    // Notify the operator.
    char msg[128];
    std::snprintf (msg, sizeof (msg),
                   "%s restarted (attempt %u of %u)",
                   state.instanceId.c_str(),
                   static_cast<unsigned> (state.restartCount),
                   static_cast<unsigned> (kMaxRestartAttempts));
    postNotification (NotificationLevel::Info, msg);
}

std::optional<PluginDescriptor>
OutOfProcessEffectChainHost::descriptorForSlot (
    std::int64_t busId, std::size_t slotIndex) const noexcept
{
    std::scoped_lock lk (instancesMutex_);
    const SlotKey key { busId, slotIndex };
    const auto it = instances_.find (key);
    if (it == instances_.end() || it->second == nullptr)
        return std::nullopt;
    const auto& state = *it->second;
    if (state.permanentlyBypassed.load (std::memory_order_acquire))
        return std::nullopt;
    // Honor the transient bypass flag too — matches pumpSlot's discipline.
    // Reading descriptor under the mutex is itself race-free, but the
    // docstring promises nullopt while bypassed and consistency with
    // stateBlobForSlot is the goal.
    if (state.bypassed.load (std::memory_order_acquire))
        return std::nullopt;
    return state.descriptor;
}

std::optional<std::vector<std::byte>>
OutOfProcessEffectChainHost::stateBlobForSlot (
    std::int64_t busId, std::size_t slotIndex) const noexcept
{
    // Matches OutOfProcessPluginInstance::requestStateSave's default; the
    // engine save path tolerates a per-slot stall of up to this long
    // before falling back to an empty pinning blob.
    constexpr std::chrono::milliseconds kStateSaveTimeout { 250 };

    std::shared_ptr<SlotState> snapshot;
    {
        std::scoped_lock lk (instancesMutex_);
        const SlotKey key { busId, slotIndex };
        const auto it = instances_.find (key);
        if (it == instances_.end() || it->second == nullptr)
            return std::nullopt;
        snapshot = it->second;
    }

    if (snapshot->permanentlyBypassed.load (std::memory_order_acquire))
        return std::nullopt;
    // Bypass-flag fence — checked AFTER releasing instancesMutex_ but
    // BEFORE dereferencing snapshot->instance. During a transient-bypass
    // restart window the supervisor mutates instance IN PLACE without
    // holding instancesMutex_; the acquire load here synchronizes-with
    // the supervisor's release store (attemptRestart) and closes the
    // unlocked data race against the in-place instance swap, exactly as
    // pumpSlot does.
    if (snapshot->bypassed.load (std::memory_order_acquire))
        return std::nullopt;
    if (snapshot->instance == nullptr)
        return std::nullopt;

    std::vector<std::byte> out;
    const bool ok = snapshot->instance->requestStateSave (out, kStateSaveTimeout);
    if (! ok)
    {
        auto* const sink = notificationSink_.load (std::memory_order_acquire);
        if (sink != nullptr)
        {
            // Enough for "state save timed out for slot <int64>/<size_t>";
            // worst-case decimal widths fit comfortably under 96 bytes.
            constexpr std::size_t kTimeoutMsgBytes = 96;
            char msg[kTimeoutMsgBytes];
            std::snprintf (msg, sizeof (msg),
                           "state save timed out for slot %lld/%zu",
                           static_cast<long long> (busId), slotIndex);
            sink->post (NotificationLevel::Warning,
                        Category::PluginEvent, msg);
        }
        return std::nullopt;
    }
    return out;
}

} // namespace sirius
