#pragma once

#include <cstdint>

namespace sirius
{

/// Identifies a single input source — which tape an event belongs to, and which
/// tape a loop Constituent plays from.
///
/// A small value type in its own header so that a Constituent can reference a
/// tape without depending on the Tape template itself.
class TapeId
{
public:
    explicit constexpr TapeId (std::int64_t value) noexcept : value_ (value) {}

    std::int64_t value() const noexcept { return value_; }

    bool operator== (const TapeId& other) const noexcept { return value_ == other.value_; }
    bool operator!= (const TapeId& other) const noexcept { return value_ != other.value_; }

private:
    std::int64_t value_;
};

} // namespace sirius
