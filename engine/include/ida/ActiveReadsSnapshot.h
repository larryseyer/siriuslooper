#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ida {

/// Max simultaneously-sounding phrase channels resolved per block (v1 = 64).
inline constexpr int kMaxPhraseSlots = 64;

/// One pre-resolved active read: which phrase-channel slot is sounding and the
/// absolute tape sample index it should read from this block. The worker has
/// already mapped ConstituentId -> OutputChannelId -> slot, so the audio thread
/// needs no map lookup.
struct PhraseSlotRead {
    int          slot            { -1 };   ///< dense phrase-channel slot index
    std::int64_t tapeSampleStart { 0 };    ///< absolute tape sample for this block's start
    bool         active          { false };
};

static_assert (std::is_trivially_copyable_v<PhraseSlotRead>);

/// Fixed-capacity snapshot. Trivially copyable so the publisher can byte-copy it.
struct ActiveReadsSnapshot {
    std::array<PhraseSlotRead, kMaxPhraseSlots> slots {};
    int count { 0 };

    void clear() noexcept { count = 0; }

    void add (const PhraseSlotRead& r) noexcept
    {
        if (count < kMaxPhraseSlots)
            slots[static_cast<std::size_t> (count++)] = r;
    }
};

static_assert (std::is_trivially_copyable_v<ActiveReadsSnapshot>);

/// Single-producer (worker) / single-consumer (audio thread) seqlock publisher.
/// The consumer read is wait-free in the common case and retries only if a write
/// was concurrent; the producer never blocks. No allocation on either side.
class ActiveReadsPublisher {
public:
    void publish (const ActiveReadsSnapshot& s) noexcept
    {
        const std::uint32_t seq = seq_.load (std::memory_order_relaxed);
        seq_.store (seq + 1, std::memory_order_release);   // odd: write in progress
        std::atomic_thread_fence (std::memory_order_release);
        buffer_ = s;                                       // POD copy
        std::atomic_thread_fence (std::memory_order_release);
        seq_.store (seq + 2, std::memory_order_release);   // even: write complete
    }

    void read (ActiveReadsSnapshot& out) const noexcept
    {
        for (;;)
        {
            const std::uint32_t before = seq_.load (std::memory_order_acquire);
            if (before & 1u) continue;                     // writer mid-write
            std::atomic_thread_fence (std::memory_order_acquire);
            out = buffer_;                                 // POD copy
            std::atomic_thread_fence (std::memory_order_acquire);
            const std::uint32_t after = seq_.load (std::memory_order_acquire);
            if (before == after) return;                   // stable read
        }
    }

private:
    mutable ActiveReadsSnapshot buffer_ {};
    std::atomic<std::uint32_t>  seq_ { 0 };
};

} // namespace ida
