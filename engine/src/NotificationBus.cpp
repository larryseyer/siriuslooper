#include "ida/NotificationBus.h"

#include <juce_core/juce_core.h>

#include <cstring>

namespace sirius
{

NotificationBus::NotificationBus()
{
    // Pre-allocate every per-category ring up front. No allocation happens
    // again for the life of the bus on any post-time or drain-time path.
    for (std::size_t i = 0; i < kCategoryCount; ++i)
    {
        rings_[i] = std::make_unique<LockFreeSpscQueue<Notification>> (kRingCapacity);
        overflowCounters_[i].store (0, std::memory_order_relaxed);
    }
}

bool NotificationBus::post (NotificationLevel level,
                            Category category,
                            const char* message) noexcept
{
    // Build the Notification on the stack. The fixed-size message buffer and
    // POD layout keep this allocation-free; the SPSC's `push(const T&)` does
    // an in-place copy into a pre-allocated slot.
    Notification notification {};
    notification.level = level;
    notification.category = category;

    // `juce::Time::getHighResolutionTicks` is a VDSO-backed userspace call on
    // Apple Silicon — bounded and allocation-free. Snapshot at post time so
    // the UI can render an accurate "when did this happen" timestamp.
    notification.postedTicks = juce::Time::getHighResolutionTicks();

    // Copy the message with truncation. `std::array::data()` is the writable
    // raw buffer. We force a null terminator at the last byte so a >127-char
    // input still terminates safely. Empty / null messages produce an
    // empty-but-null-terminated buffer.
    constexpr std::size_t maxBytes = sizeof (notification.message);
    static_assert (maxBytes >= 1, "Notification::message must have room for at least the null terminator.");

    if (message != nullptr)
    {
        std::size_t copied = 0;
        for (; copied + 1 < maxBytes && message[copied] != '\0'; ++copied)
            notification.message[copied] = message[copied];
        notification.message[copied] = '\0';
    }
    else
    {
        notification.message[0] = '\0';
    }

    // Force null termination at the final byte for the truncation case. If
    // the message fit entirely, this overwrites a byte that was already
    // zero-initialized; if it was truncated, this is the guarantee.
    notification.message[maxBytes - 1] = '\0';

    auto& ring = *rings_[index (category)];
    const bool accepted = ring.push (notification);
    if (! accepted)
    {
        // Overflow: bump the per-category counter and drop the NEW entry.
        // See header comment on `post` for why dropping NEW (not OLDEST) is
        // the only SPSC-correct policy.
        overflowCounters_[index (category)].fetch_add (1, std::memory_order_relaxed);
    }
    return accepted;
}

void NotificationBus::drain (std::vector<Notification>& out)
{
    out.clear();
    // Walk each category once. Per-ring FIFO is preserved by SPSC; cross-
    // category order is interleaved by walk order, which is fine per V5 §8.6.
    for (std::size_t i = 0; i < kCategoryCount; ++i)
    {
        auto& ring = *rings_[i];
        Notification notification {};
        while (ring.pop (notification))
            out.push_back (notification);
    }
}

std::uint64_t NotificationBus::overflowCount (Category category) const noexcept
{
    return overflowCounters_[index (category)].load (std::memory_order_relaxed);
}

} // namespace sirius
