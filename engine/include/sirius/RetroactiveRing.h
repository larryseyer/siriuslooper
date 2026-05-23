#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace sirius
{

/// The in-memory window of recent tape data (white paper Part 6.4).
///
/// Because the tapes are always running, the user never has to "start
/// recording before the moment they want to capture" — the moment is already
/// here. The retroactive ring holds the most recent events so a boundary can be
/// pulled *backward* in time to grab the pickup note that preceded a mark. When
/// the ring is full, each new event overwrites the oldest: depth is bounded by
/// the capability tier, not unbounded.
///
/// This lives on the engine (consumer) side, after the lock-free queue has been
/// drained — it is single-threaded and is not itself the real-time path.
template <typename T>
class RetroactiveRing
{
public:
    /// Constructs a ring holding at most `capacity` events. Throws
    /// std::invalid_argument if `capacity` is zero.
    explicit RetroactiveRing (std::size_t capacity)
        : buffer_ (capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument ("ida::RetroactiveRing: capacity must be positive");
    }

    /// Appends an event. When the ring is full, the oldest event is overwritten.
    void push (const T& item)
    {
        buffer_[(head_ + count_) % buffer_.size()] = item;

        if (count_ < buffer_.size())
            ++count_;
        else
            head_ = (head_ + 1) % buffer_.size();
    }

    std::size_t capacity() const noexcept { return buffer_.size(); }
    std::size_t size()     const noexcept { return count_; }
    bool        empty()    const noexcept { return count_ == 0; }
    bool        full()     const noexcept { return count_ == buffer_.size(); }

    /// The events currently held, oldest first.
    std::vector<T> snapshot() const
    {
        std::vector<T> result;
        result.reserve (count_);
        for (std::size_t i = 0; i < count_; ++i)
            result.push_back (buffer_[(head_ + i) % buffer_.size()]);
        return result;
    }

private:
    std::vector<T> buffer_;
    std::size_t head_ { 0 };  // index of the oldest event
    std::size_t count_ { 0 }; // number of valid events held
};

} // namespace sirius
