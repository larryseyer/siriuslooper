#pragma once

#include "sirius/LockFreeSpscQueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace sirius
{

/// Severity of a `Notification`. The four levels are the V5 §8.6 contract:
/// `Info` is a cue (e.g. tape rotation completed), `Degradation` signals a
/// shed (UI was rendered at half rate to keep audio sacred), `Warning`
/// signals something the operator should look at soon (disk approaching
/// fill), and `Error` signals a failure semantic per V7 §17.9 (a tape write
/// failed, a device dropped).
enum class NotificationLevel
{
    Info,
    Degradation,
    Warning,
    Error
};

/// Truthfulness-channel category. One SPSC ring per category prevents
/// priority inversion: a flood of `CpuPressure` posts cannot starve a single
/// `DeviceEvent`. The list mirrors V5 §8.6 exactly — do not extend without
/// updating the white paper and the per-category storage in `NotificationBus`.
enum class Category
{
    DiskPressure,
    CpuPressure,
    RamPressure,
    DeviceEvent,
    PluginEvent,
    ClockEvent,
    NetworkEvent,
    StateRepair,
    TapeRotation
};

/// The number of `Category` values. Keep in sync with the enum above; the bus
/// constructor uses this to size the per-category storage. The `static_assert`
/// below pins the count to the last enumerator so adding a new Category
/// without bumping the count is a compile error rather than silent storage
/// truncation (CLAUDE.md rule 8 — fail loud).
inline constexpr std::size_t kCategoryCount = 9;
static_assert (static_cast<std::size_t> (Category::TapeRotation) + 1 == kCategoryCount,
               "kCategoryCount must equal the number of Category enumerators "
               "— if you added a Category, bump kCategoryCount and update the "
               "RT_SAFETY_CONTRACT §6 row");

/// One engine→UI truthfulness signal. Trivially copyable so the SPSC ring's
/// in-place copy on `push` stays allocation-free and bounded — verified by a
/// `static_assert` below. Layout is deliberately POD: enums first, then the
/// 64-bit tick snapshot, then the fixed-size message buffer. Total size is
/// ~144 bytes — fits comfortably in a cache line group on Apple Silicon.
///
/// `postedTicks` is a `juce::Time::getHighResolutionTicks()` snapshot taken
/// at post time inside `NotificationBus::post`. Stored as `std::int64_t` (not
/// `Lmc::Rational`) because notifications are wall-clock UI signals, not
/// music-time-anchored events — the UI converts ticks to a readable timestamp
/// at render time.
///
/// `message` is null-terminated within the 128-byte array; longer inputs are
/// truncated and the last byte is forced to `'\0'`.
struct Notification
{
    NotificationLevel level;
    Category category;
    std::int64_t postedTicks;
    std::array<char, 128> message;
};

static_assert (std::is_trivially_copyable_v<Notification>,
               "sirius::Notification must be trivially copyable so the per-category "
               "LockFreeSpscQueue<Notification> push remains allocation-free and bounded "
               "on the audio thread.");

/// Engine↔UI truthfulness channel (V5 §8.6, V7 §17.9). The audio thread (and
/// any other thread) posts notifications via `post`, which is allocation-free,
/// lock-free, and `noexcept`. The message thread drains via `drain`.
///
/// Storage is one `LockFreeSpscQueue<Notification>` per `Category`. The bus
/// pre-allocates all nine rings (capacity 256 each) in the constructor — no
/// allocation happens on any post-time or drain-time path.
///
/// Threading contract:
///   - `post` is single-producer per category from the audio thread's POV;
///     while the bus does not statically prevent two threads from posting
///     into the same category simultaneously, the V7 design has the audio
///     thread as the sole post-side producer for `CpuPressure`,
///     `DiskPressure`, and similar — message-thread post sites target
///     categories the audio thread does not write.
///   - `drain` is single-consumer (the message-thread UI timer).
///   - `overflowCount` is safe to read from any thread (atomic load).
///
/// Each category SPSC has its own head/tail atomics — the bus does NOT
/// serialize across categories. This is the priority-inversion-free property
/// called out in the M6 spec.
class NotificationBus
{
public:
    /// Per-category ring capacity (V7 alignment plan M6 line 479).
    static constexpr std::size_t kRingCapacity = 256;

    /// Allocates the nine per-category SPSC rings + overflow counters.
    /// Throws nothing the underlying `LockFreeSpscQueue` does not throw
    /// (only `std::bad_alloc` on the message thread at construction).
    NotificationBus();

    NotificationBus (const NotificationBus&) = delete;
    NotificationBus& operator= (const NotificationBus&) = delete;

    /// Audio-thread-safe post. Snapshots `juce::Time::getHighResolutionTicks()`
    /// for `postedTicks`, copies `message` into the fixed-size buffer with
    /// truncation (and a forced null terminator at the last byte), and pushes
    /// onto the SPSC for `category`. Returns true if accepted, false if the
    /// category ring is full (in which case `overflowCount(category)` is
    /// incremented, and the NEW entry is dropped).
    ///
    /// Deviation from M6 plan line 479: the plan calls for "drops the oldest
    /// entry" on overflow. Implementing drop-oldest would require the
    /// producer to touch the consumer's head pointer, which violates the
    /// `LockFreeSpscQueue` single-producer / single-consumer contract and
    /// would race the drain thread. Dropping the NEW entry is the only
    /// SPSC-correct policy. The overflow counter is the operator-visible
    /// signal in either policy, so the diagnostic surface is preserved.
    ///
    /// `noexcept` and allocation-free. Safe to call from the audio thread.
    /// `message` may be null — null is treated as empty string.
    bool post (NotificationLevel level, Category category, const char* message) noexcept;

    /// Message-thread drain. Walks each category once in declaration order,
    /// popping all available notifications into `out`. Per-category ordering
    /// is preserved (SPSC guarantees per-ring FIFO); cross-category ordering
    /// is NOT preserved (and not required by V5 §8.6).
    ///
    /// `out` is `clear()`ed up-front. The caller MUST `reserve()` enough
    /// capacity on first use to hold a full drain (worst case
    /// `kCategoryCount * 256 = 2304` notifications) — `std::vector::push_back`
    /// can throw `std::bad_alloc` if the vector needs to reallocate, and
    /// drain is NOT `noexcept` so the exception propagates rather than
    /// terminating. Per CLAUDE.md rule 8 (fail loud), a caller that
    /// under-reserves will see a real exception instead of silent corruption.
    void drain (std::vector<Notification>& out);

    /// How many posts have been dropped on overflow for `category`. Atomic
    /// load with `relaxed` ordering — exact-but-eventually-consistent across
    /// threads is the operator-visible contract.
    std::uint64_t overflowCount (Category category) const noexcept;

private:
    /// Index helper. `Category` enumerators are dense 0..8, so `static_cast`
    /// is sufficient.
    static constexpr std::size_t index (Category category) noexcept
    {
        return static_cast<std::size_t> (category);
    }

    /// One SPSC ring per category. Held by `std::unique_ptr` because
    /// `LockFreeSpscQueue` is non-copyable / non-movable (its `std::atomic`
    /// members forbid it) — `std::array` of nine unique_ptr keeps the layout
    /// flat and the dispatch O(1).
    std::array<std::unique_ptr<LockFreeSpscQueue<Notification>>, kCategoryCount> rings_;

    /// One overflow counter per category. `std::atomic<std::uint64_t>` so
    /// `overflowCount` can read consistently from any thread.
    std::array<std::atomic<std::uint64_t>, kCategoryCount> overflowCounters_ {};
};

} // namespace sirius
