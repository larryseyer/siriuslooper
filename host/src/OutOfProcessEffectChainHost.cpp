#include "sirius/OutOfProcessEffectChainHost.h"

#include "sirius/PluginInstanceId.h"
#include "sirius/PluginIpcMessage.h"

#include <juce_core/juce_core.h>

#include <algorithm>
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
}

OutOfProcessEffectChainHost::OutOfProcessEffectChainHost()
    : wireScratch_ (PluginIpcMessage::kMaxPayloadBytes)
{
    // Modest reservation to keep configureBus's emplace from rehashing
    // for typical small-chain populations.
    instances_.reserve (16);
}

OutOfProcessEffectChainHost::~OutOfProcessEffectChainHost() = default;

void OutOfProcessEffectChainHost::configureBus (std::int64_t       busId,
                                                const EffectChain& chain,
                                                const juce::File&  hostBinary,
                                                const juce::File&  clapBundle)
{
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

    // Spawn an instance for each non-bypassed slot that doesn't already
    // have one. Bypassed slots are not spawned at all — the audio thread
    // would skip them via pumpSlot's missing-instance branch anyway, but
    // not spawning is cheaper and matches the "no resources for inactive
    // slots" principle.
    const auto& entries = chain.entries();
    for (std::size_t slotIdx = 0; slotIdx < entries.size(); ++slotIdx)
    {
        if (entries[slotIdx].bypassed)
            continue;

        const SlotKey key { busId, slotIdx };
        if (instances_.find (key) != instances_.end())
            continue; // already have one

        const std::string instanceId = makeInstanceId (busId, slotIdx);
        instances_.emplace (
            key,
            std::make_unique<OutOfProcessPluginInstance> (
                hostBinary, instanceId, clapBundle));
    }
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

    // Lookup the slot's instance. The map is NOT mutated on the audio
    // thread (configureBus is message-thread-only, runs before audio
    // starts per the M5/M6 collaborator contract). find() on an
    // unordered_map is O(1) average, allocation-free, and noexcept on
    // libc++ / libstdc++ for trivially-copyable keys.
    const SlotKey key { busId, slotIndex };
    const auto it = instances_.find (key);
    if (it == instances_.end() || it->second == nullptr)
        return false; // no instance for this slot — dry-on-miss

    auto& instance = *it->second;

    // Package the input buffer into the CLAP-mode wire format the
    // sirius_plugin_host child expects (matches host_process/main.cpp
    // `runClapMode`): `uint32_t frameCount` followed by `frameCount ×
    // kPumpChannels × float` interleaved samples. The whole payload
    // fits in one PluginIpcMessage as long as it's under
    // `kMaxPayloadBytes` (8192 bytes → with the 4-byte header that's
    // frameCount ≤ 1023 for stereo — the V7 plan's outer 1024-frame
    // envelope is one frame short of fitting; realistic block sizes
    // are 64..512 and pass with headroom).
    constexpr std::size_t kHeaderBytes  = sizeof (std::uint32_t);
    const std::size_t     interleavedBytes =
        static_cast<std::size_t> (numSamples) * kPumpChannels * sizeof (float);
    const std::size_t     totalBytes  = kHeaderBytes + interleavedBytes;
    if (totalBytes > PluginIpcMessage::kMaxPayloadBytes)
        return false; // oversized — host can't accept

    // Write the header (frameCount). Stored as uint32_t LE per the
    // host's `readExact (..., sizeof(frameCount))` shape.
    const std::uint32_t frameCount = static_cast<std::uint32_t> (numSamples);
    std::memcpy (wireScratch_.data(), &frameCount, kHeaderBytes);

    // Interleave the input channels into the wire format. If the caller
    // passed mono (channels == 1), duplicate to L and R so the host's
    // stereo CLAP buffers stay populated.
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

    // Push this buffer onto the engine→host ring. On a full ring, just
    // return false — pipelined model accepts that a transient stall
    // means one dry buffer.
    (void) instance.tryWriteBytes (wireScratch_.data(), totalBytes);

    // Pop the response for the PREVIOUS buffer (pipelined 1-buffer
    // delay). The host's response format is the raw interleaved float
    // payload (no `uint32_t frameCount` header — the host's
    // `RingByteStream::writeAll` + `flush` pair writes only the
    // post-process audio bytes, see host_process/main.cpp:438-444). On
    // a miss, leave outChannels untouched (caller's dry-on-miss
    // contract).
    std::size_t bytesRead = 0;
    const bool popped = instance.tryReadBytes (
        wireScratch_.data(), wireScratch_.size(), bytesRead);
    if (! popped) return false;
    if (bytesRead == 0) return false;

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

    return framesAvailable > 0;
}

} // namespace sirius
