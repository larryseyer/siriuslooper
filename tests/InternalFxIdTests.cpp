// Pins the four built-in FX ids + the round-trip string discriminant used by
// SessionFormat. The enum is the wire-stable identity of an Internal slot —
// renaming a value would break every saved session that contains an Internal
// effect, so these tests serve as the contract for both readers and writers.
#include "sirius/InternalFxId.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

using ida::InternalFxId;

TEST_CASE ("InternalFxId enum values are stable", "[internal-fx-id]")
{
    // Pinned to guard against reordering — the discriminant order must match the
    // string-to-id mapping and the reserved-range contract in InternalFxId.h.
    CHECK (static_cast<std::uint8_t> (InternalFxId::kEq)  == 0);
    CHECK (static_cast<std::uint8_t> (InternalFxId::kCmp) == 1);
    CHECK (static_cast<std::uint8_t> (InternalFxId::kRvb) == 2);
    CHECK (static_cast<std::uint8_t> (InternalFxId::kDly) == 3);
}

TEST_CASE ("InternalFxId round-trips through toString / fromString", "[internal-fx-id]")
{
    for (auto id : { InternalFxId::kEq, InternalFxId::kCmp,
                     InternalFxId::kRvb, InternalFxId::kDly })
    {
        CHECK (ida::internalFxIdFromString (ida::internalFxIdToString (id)) == id);
    }
}

TEST_CASE ("internalFxIdToString produces the documented strings", "[internal-fx-id]")
{
    // These strings are the wire format. Do not rename them without a
    // forward-compat plan in SessionFormat.
    CHECK (ida::internalFxIdToString (InternalFxId::kEq)  == std::string ("EQ"));
    CHECK (ida::internalFxIdToString (InternalFxId::kCmp) == std::string ("CMP"));
    CHECK (ida::internalFxIdToString (InternalFxId::kRvb) == std::string ("RVB"));
    CHECK (ida::internalFxIdToString (InternalFxId::kDly) == std::string ("DLY"));
}

TEST_CASE ("internalFxIdFromString throws on unknown id", "[internal-fx-id]")
{
    CHECK_THROWS_AS (ida::internalFxIdFromString ("Synth"),  std::invalid_argument);
    CHECK_THROWS_AS (ida::internalFxIdFromString (""),       std::invalid_argument);
}
