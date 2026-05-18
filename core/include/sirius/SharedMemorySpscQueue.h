#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace sirius
{

/// A bounded, wait-free, single-producer / single-consumer queue whose
/// ring storage lives inside a caller-supplied memory region — the
/// cross-process variant of `LockFreeSpscQueue<T>`.
///
/// `LockFreeSpscQueue<T>` allocates its `T[]` buffer in heap memory, which
/// makes it useless across a process boundary (the heap is per-process).
/// `SharedMemorySpscQueue<T>` instead places its head/tail atomics and its
/// T[] buffer inside a memory region the caller provides — typically a
/// POSIX shared-memory segment mapped via `SharedMemoryRegion`. Both ends
/// of the IPC channel construct one of these against the same region.
///
/// Producer side does `create(base, capacity)` once (placement-new of the
/// atomics into zero-filled bytes); consumer side does `attach(base,
/// capacity)`. From there `push` / `pop` semantics match
/// `LockFreeSpscQueue<T>` byte-for-byte (drop-NEW overflow, wait-free,
/// FIFO).
///
/// Constraints on `T`:
///   - trivially copyable AND trivially destructible — items live in
///     shared memory across process lifetimes; destructors won't run.
///   - default-constructible — buffer slots start zero-initialised.
/// These match the M6 NotificationBus / M7 plug-in IPC payload shapes
/// (fixed-size PODs).
///
/// **Wait-freedom holds only for one producer + one consumer**, exactly
/// as with `LockFreeSpscQueue<T>`. Plug-in IPC channels are SPSC by
/// construction (one engine thread pushes; one host pump thread pops).
template <typename T>
class SharedMemorySpscQueue
{
    static_assert (std::is_trivially_copyable_v<T>,
                   "SharedMemorySpscQueue<T> requires trivially copyable T (lives in shared memory)");
    static_assert (std::is_trivially_destructible_v<T>,
                   "SharedMemorySpscQueue<T> requires trivially destructible T (destructors do not cross process boundaries)");
    static_assert (std::is_default_constructible_v<T>,
                   "SharedMemorySpscQueue<T> requires default-constructible T (zero-init slot semantics)");

public:
    /// Bytes the caller must mmap (or otherwise allocate) before
    /// constructing the queue against the region. Includes the head/tail
    /// atomics, the (capacity + 1) buffer slots, and any padding.
    static constexpr std::size_t bytesNeeded (std::size_t capacity) noexcept
    {
        return sizeof (Layout) + (capacity + 1) * sizeof (T);
    }

    /// Producer-side construction. `base` must point at `bytesNeeded(capacity)`
    /// zero-filled bytes; the call placement-news the head/tail atomics and
    /// returns a queue that owns the writer end. Throws on zero capacity.
    static SharedMemorySpscQueue create (void* base, std::size_t capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument ("sirius::SharedMemorySpscQueue: capacity must be positive");
        if (base == nullptr)
            throw std::invalid_argument ("sirius::SharedMemorySpscQueue: base must be non-null");

        auto* layout = new (base) Layout;
        layout->capacityPlusOne = capacity + 1;
        // The T[capacity + 1] storage that follows `Layout` starts as
        // zero-filled bytes (per the caller's contract). Trivially-
        // copyable + default-constructible T means the zero bytes are
        // a valid default-initialised value.
        return SharedMemorySpscQueue (layout);
    }

    /// Consumer-side construction. Attaches to a layout the producer has
    /// already initialised. No placement-new; reads the head/tail atomics
    /// in place.
    static SharedMemorySpscQueue attach (void* base, std::size_t capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument ("sirius::SharedMemorySpscQueue: capacity must be positive");
        if (base == nullptr)
            throw std::invalid_argument ("sirius::SharedMemorySpscQueue: base must be non-null");

        auto* layout = static_cast<Layout*> (base);
        // Sanity-check that the producer wrote a matching capacity. A
        // mismatch means the two sides disagree on the segment shape —
        // probably a stale segment from a previous instance.
        if (layout->capacityPlusOne != capacity + 1)
            throw std::invalid_argument (
                "sirius::SharedMemorySpscQueue: attach capacity mismatch with producer-written layout");
        return SharedMemorySpscQueue (layout);
    }

    SharedMemorySpscQueue (const SharedMemorySpscQueue&)            = delete;
    SharedMemorySpscQueue& operator= (const SharedMemorySpscQueue&) = delete;
    SharedMemorySpscQueue (SharedMemorySpscQueue&&) noexcept        = default;
    SharedMemorySpscQueue& operator= (SharedMemorySpscQueue&&) noexcept = default;

    /// Producer side. Returns false if the queue is full. Wait-free.
    bool push (const T& item) noexcept
    {
        const std::size_t tail = layout_->tail.load (std::memory_order_relaxed);
        const std::size_t next = increment (tail);
        if (next == layout_->head.load (std::memory_order_acquire))
            return false;
        slot (tail) = item;
        layout_->tail.store (next, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false if the queue is empty; otherwise copies
    /// the oldest item into `out`. Wait-free.
    bool pop (T& out) noexcept
    {
        const std::size_t head = layout_->head.load (std::memory_order_relaxed);
        if (head == layout_->tail.load (std::memory_order_acquire))
            return false;
        out = slot (head);
        layout_->head.store (increment (head), std::memory_order_release);
        return true;
    }

    /// Usable capacity (matches the `capacity` arg passed to create/attach).
    std::size_t capacity() const noexcept { return layout_->capacityPlusOne - 1; }

    /// Racy snapshot — definitive only from the calling thread's own side.
    bool empty() const noexcept
    {
        return layout_->head.load (std::memory_order_acquire)
            == layout_->tail.load (std::memory_order_acquire);
    }

private:
    /// Header struct placement-new'd at the start of the region. Followed
    /// by `capacityPlusOne` `T` slots in memory. Sized + ordered so the
    /// atomics sit on their natural alignment under any sane ABI; the T
    /// array begins immediately after the header.
    struct Layout
    {
        std::atomic<std::size_t> head { 0 };          // consumer owns
        std::atomic<std::size_t> tail { 0 };          // producer owns
        std::size_t              capacityPlusOne { 0 }; // sanity check on attach
    };

    explicit SharedMemorySpscQueue (Layout* layout) noexcept : layout_ (layout) {}

    std::size_t increment (std::size_t i) const noexcept
    {
        const auto cap = layout_->capacityPlusOne;
        return (i + 1) % cap;
    }

    T& slot (std::size_t index) noexcept
    {
        // Storage immediately follows the layout header.
        auto* base = reinterpret_cast<std::byte*> (layout_) + sizeof (Layout);
        return reinterpret_cast<T*> (base)[index];
    }

    const T& slot (std::size_t index) const noexcept
    {
        const auto* base = reinterpret_cast<const std::byte*> (layout_) + sizeof (Layout);
        return reinterpret_cast<const T*> (base)[index];
    }

    Layout* layout_ { nullptr };
};

} // namespace sirius
