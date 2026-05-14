#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sirius
{

/// A bounded, wait-free, single-producer / single-consumer queue.
///
/// This is the real-time-safe tape-write path (white paper Part VI, M2): the
/// audio thread is the sole *producer* — it pushes captured events without ever
/// locking, allocating, or blocking — and a non-real-time engine thread is the
/// sole *consumer*, draining events into the tape and the retroactive ring.
///
/// Wait-freedom holds only for one producer and one consumer. `T` must be
/// default-constructible; for the producer to stay real-time-safe, `T`'s copy
/// or move must itself be allocation-free and lock-free (a small trivially
/// copyable event type). The buffer is allocated once, at construction, and
/// never resized.
template <typename T>
class LockFreeSpscQueue
{
public:
    /// Allocates room for `capacity` items. One extra slot is reserved
    /// internally to tell "full" from "empty", so the buffer holds capacity+1.
    /// Throws std::invalid_argument if `capacity` is zero.
    explicit LockFreeSpscQueue (std::size_t capacity)
        : buffer_ (capacity + 1)
    {
        if (capacity == 0)
            throw std::invalid_argument ("sirius::LockFreeSpscQueue: capacity must be positive");
    }

    LockFreeSpscQueue (const LockFreeSpscQueue&) = delete;
    LockFreeSpscQueue& operator= (const LockFreeSpscQueue&) = delete;

    /// Producer side. Returns false if the queue is full. Wait-free.
    bool push (const T& item)
    {
        const std::size_t tail = tail_.load (std::memory_order_relaxed);
        const std::size_t next = increment (tail);
        if (next == head_.load (std::memory_order_acquire))
            return false; // full

        buffer_[tail] = item;
        tail_.store (next, std::memory_order_release);
        return true;
    }

    /// Producer side, moving overload. Returns false if the queue is full.
    bool push (T&& item)
    {
        const std::size_t tail = tail_.load (std::memory_order_relaxed);
        const std::size_t next = increment (tail);
        if (next == head_.load (std::memory_order_acquire))
            return false; // full

        buffer_[tail] = std::move (item);
        tail_.store (next, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false if the queue is empty; otherwise moves the
    /// oldest item into `out`. Wait-free.
    bool pop (T& out)
    {
        const std::size_t head = head_.load (std::memory_order_relaxed);
        if (head == tail_.load (std::memory_order_acquire))
            return false; // empty

        out = std::move (buffer_[head]);
        head_.store (increment (head), std::memory_order_release);
        return true;
    }

    /// The number of items the queue can hold (the usable capacity).
    std::size_t capacity() const noexcept { return buffer_.size() - 1; }

    /// A racy snapshot of whether the queue currently looks empty. Only the
    /// definitive answer from the calling thread's own side is reliable.
    bool empty() const noexcept
    {
        return head_.load (std::memory_order_acquire)
            == tail_.load (std::memory_order_acquire);
    }

private:
    std::size_t increment (std::size_t index) const noexcept
    {
        return (index + 1) % buffer_.size();
    }

    std::vector<T> buffer_;
    std::atomic<std::size_t> head_ { 0 }; // consumer owns; oldest item
    std::atomic<std::size_t> tail_ { 0 }; // producer owns; next write slot
};

} // namespace sirius
