// Golden-value tests for sirius::arrangement — the M3 arrangement primitives
// (white paper Part 11.3). These pin down the two claims that make the
// primitives trustworthy: they are pure Constituent-tree placement operations
// (sequencing puts children end-to-end, layering puts them simultaneously),
// and they preserve identity — a sequenced or layered child is the same
// Constituent, only repositioned. RoleSlot is the one genuinely new type: a
// role-fillable position resolved at play time (white paper Part 8.4).
#include "sirius/Arrangement.h"

#include "sirius/Constituent.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/RenderPipeline.h"
#include "sirius/TapeReference.h"
#include "sirius/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::Position;
using sirius::Rational;
using sirius::RenderPipeline;
using sirius::RoleSlot;
using sirius::TapeId;
using sirius::TapeReference;
using sirius::TempoMap;

namespace
{
    /// A leaf Constituent with id `id` spanning [in, out) whole notes. The
    /// arbitrary placement lets the tests prove that sequence/layer reposition
    /// children rather than trusting their incoming placement.
    std::shared_ptr<const Constituent> makeChild (std::int64_t id, Rational in, Rational out)
    {
        return std::make_shared<const Constituent> (
            ConstituentId (id), Position (in), Position (out));
    }
}

TEST_CASE ("sequence places children end-to-end, preserving each child's duration",
           "[arrangement][sequence]")
{
    // Children carry deliberately awkward incoming placements; sequencing must
    // reposition them while keeping each one's own duration intact.
    const Constituent parent (ConstituentId (1), Position(), Position (Rational (10)));
    const auto a = makeChild (10, Rational (3), Rational (5));  // duration 2
    const auto b = makeChild (11, Rational (0), Rational (1));  // duration 1
    const auto c = makeChild (12, Rational (8), Rational (12)); // duration 4

    const Constituent arranged = sirius::arrangement::sequence (parent, { a, b, c });

    REQUIRE (arranged.children().size() == 3);
    CHECK (arranged.children()[0]->conceptualIn()  == Position (Rational (0)));
    CHECK (arranged.children()[0]->conceptualOut() == Position (Rational (2)));
    CHECK (arranged.children()[1]->conceptualIn()  == Position (Rational (2)));
    CHECK (arranged.children()[1]->conceptualOut() == Position (Rational (3)));
    CHECK (arranged.children()[2]->conceptualIn()  == Position (Rational (3)));
    CHECK (arranged.children()[2]->conceptualOut() == Position (Rational (7)));

    // Identity survives placement — same Constituents, only repositioned.
    CHECK (arranged.children()[0]->id() == ConstituentId (10));
    CHECK (arranged.children()[1]->id() == ConstituentId (11));
    CHECK (arranged.children()[2]->id() == ConstituentId (12));
}

TEST_CASE ("sequence is copy-on-write and composable across calls",
           "[arrangement][sequence]")
{
    const Constituent parent (ConstituentId (1), Position(), Position (Rational (10)));
    const auto a = makeChild (10, Rational (0), Rational (2));
    const auto b = makeChild (11, Rational (0), Rational (3));

    const Constituent first = sirius::arrangement::sequence (parent, { a });
    // A second sequence continues where the existing children end, so an
    // arrangement can be built up incrementally.
    const Constituent second = sirius::arrangement::sequence (first, { b });

    CHECK (parent.children().empty());            // the argument is untouched
    CHECK (first.children().size() == 1);         // the intermediate is untouched
    REQUIRE (second.children().size() == 2);
    CHECK (second.children()[1]->conceptualIn()  == Position (Rational (2)));
    CHECK (second.children()[1]->conceptualOut() == Position (Rational (5)));
}

TEST_CASE ("layer places children simultaneously at the parent's start",
           "[arrangement][layer]")
{
    const Constituent parent (ConstituentId (1), Position(), Position (Rational (10)));
    const auto a = makeChild (10, Rational (4), Rational (6));  // duration 2
    const auto b = makeChild (11, Rational (1), Rational (5));  // duration 4

    const Constituent arranged = sirius::arrangement::layer (parent, { a, b });

    REQUIRE (arranged.children().size() == 2);
    // Both start together; each keeps its own duration, so they overlap.
    CHECK (arranged.children()[0]->conceptualIn()  == Position (Rational (0)));
    CHECK (arranged.children()[0]->conceptualOut() == Position (Rational (2)));
    CHECK (arranged.children()[1]->conceptualIn()  == Position (Rational (0)));
    CHECK (arranged.children()[1]->conceptualOut() == Position (Rational (4)));
    CHECK (parent.children().empty()); // copy-on-write — the argument is untouched
}

TEST_CASE ("sequence and layer reject null children and accept an empty list",
           "[arrangement]")
{
    const Constituent parent (ConstituentId (1), Position(), Position (Rational (4)));

    CHECK_THROWS_AS (sirius::arrangement::sequence (parent, { nullptr }),
                     std::invalid_argument);
    CHECK_THROWS_AS (sirius::arrangement::layer (parent, { nullptr }),
                     std::invalid_argument);

    CHECK (sirius::arrangement::sequence (parent, {}).children().empty());
    CHECK (sirius::arrangement::layer (parent, {}).children().empty());
}

TEST_CASE ("a sequenced arrangement plays its children one after another",
           "[arrangement][sequence][renderpipeline]")
{
    // Two free-running leaf loops, sequenced under a 4-whole-note session. At
    // 120 BPM a whole note is 2 LMC seconds, so each 2-whole-note loop occupies
    // 4 seconds: loop A sounds over [0,4) s, loop B over [4,8) s.
    const Constituent loopProto (ConstituentId (0), Position(), Position (Rational (2)));
    const auto a = std::make_shared<const Constituent> (
        loopProto.withTapeReference (TapeReference (TapeId (100), Rational (0), Rational (4))));
    const auto b = std::make_shared<const Constituent> (
        Constituent (ConstituentId (1), Position(), Position (Rational (2)))
            .withTapeReference (TapeReference (TapeId (200), Rational (10), Rational (14))));

    const Constituent session (ConstituentId (9), Position(), Position (Rational (4)));
    const auto arranged = std::make_shared<const Constituent> (
        sirius::arrangement::sequence (session, { a, b }));

    const RenderPipeline pipeline (arranged, TempoMap::fromBpm (Rational (120)));

    SECTION ("inside the first child's span, only the first loop sounds")
    {
        const auto reads = pipeline.activeReadsAt (Rational (1));
        REQUIRE (reads.size() == 1);
        CHECK (reads[0].loop == ConstituentId (0));
        CHECK (reads[0].tape == TapeId (100));
        CHECK (reads[0].tapePosition == Rational (1)); // tapeIn 0 + 1 s elapsed
    }

    SECTION ("inside the second child's span, only the second loop sounds")
    {
        const auto reads = pipeline.activeReadsAt (Rational (5));
        REQUIRE (reads.size() == 1);
        CHECK (reads[0].loop == ConstituentId (1));
        CHECK (reads[0].tape == TapeId (200));
        CHECK (reads[0].tapePosition == Rational (11)); // tapeIn 10 + 1 s into child
    }
}

TEST_CASE ("a RoleSlot names a role and a span", "[arrangement][roleslot]")
{
    const RoleSlot slot ("guitar solo", Position (Rational (4)), Position (Rational (12)));

    CHECK (slot.role() == "guitar solo");
    CHECK (slot.conceptualIn()  == Position (Rational (4)));
    CHECK (slot.conceptualOut() == Position (Rational (12)));
    CHECK (slot.duration() == Rational (8));
    CHECK_FALSE (slot.isFilled());
    CHECK_FALSE (slot.filledBy().has_value());
}

TEST_CASE ("a RoleSlot cannot end before it starts", "[arrangement][roleslot]")
{
    CHECK_THROWS_AS (RoleSlot ("solo", Position (Rational (5)), Position (Rational (2))),
                     std::invalid_argument);
}

TEST_CASE ("a RoleSlot is filled and cleared copy-on-write", "[arrangement][roleslot]")
{
    const RoleSlot empty ("solo", Position(), Position (Rational (4)));

    const RoleSlot filled = empty.withFilledBy (ConstituentId (77));
    REQUIRE (filled.isFilled());
    CHECK (filled.filledBy().value() == ConstituentId (77));
    CHECK (filled.role() == "solo");        // the rest of the slot is unchanged
    CHECK (filled.duration() == Rational (4));
    CHECK_FALSE (empty.isFilled());         // the original is untouched

    const RoleSlot cleared = filled.withoutFill();
    CHECK_FALSE (cleared.isFilled());
    CHECK (filled.isFilled());              // and that edit did not touch `filled`
}
