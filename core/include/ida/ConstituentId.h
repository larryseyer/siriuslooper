#pragma once

#include <cstdint>

namespace ida
{

/// A Constituent's persistent identity. It survives every content revision: a
/// phrase named "the verse" remains the same phrase, with the same id, through
/// fifteen revisions of its content (white paper Part 7.6). Versioning happens
/// at the content level; identity persists at the structural level.
///
/// This is a small value type in its own header so that repetition rules and
/// phrase metadata — which reference other Constituents by id — can depend on
/// identity without depending on the whole Constituent definition.
class ConstituentId
{
public:
    explicit constexpr ConstituentId (std::int64_t value) noexcept : value_ (value) {}

    std::int64_t value() const noexcept { return value_; }

    bool operator== (const ConstituentId& other) const noexcept { return value_ == other.value_; }
    bool operator!= (const ConstituentId& other) const noexcept { return value_ != other.value_; }

private:
    std::int64_t value_;
};

} // namespace ida
