// Golden-value tests for ida::arrangement — the M3 arrangement primitives
// (white paper Part 11.3). These pin down the two claims that make the
// primitives trustworthy: they are pure Constituent-tree placement operations
// (sequencing puts children end-to-end, layering puts them simultaneously),
// and they preserve identity — a sequenced or layered child is the same
// Constituent, only repositioned. RoleSlot is the one genuinely new type: a
// role-fillable position resolved at play time (white paper Part 8.4).
#include "ida/Arrangement.h"

#include "ida/Constituent.h"
#include "ida/Phrase.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/RenderPipeline.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>

using ida::Constituent;
using ida::ConstituentId;
using ida::Position;
using ida::Rational;
using ida::RenderPipeline;
using ida::RoleSlot;
using ida::TapeId;
using ida::TapeReference;
using ida::TempoMap;

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

    const Constituent arranged = ida::arrangement::sequence (parent, { a, b, c });

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

    const Constituent first = ida::arrangement::sequence (parent, { a });
    // A second sequence continues where the existing children end, so an
    // arrangement can be built up incrementally.
    const Constituent second = ida::arrangement::sequence (first, { b });

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

    const Constituent arranged = ida::arrangement::layer (parent, { a, b });

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

    CHECK_THROWS_AS (ida::arrangement::sequence (parent, { nullptr }),
                     std::invalid_argument);
    CHECK_THROWS_AS (ida::arrangement::layer (parent, { nullptr }),
                     std::invalid_argument);

    CHECK (ida::arrangement::sequence (parent, {}).children().empty());
    CHECK (ida::arrangement::layer (parent, {}).children().empty());
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
        ida::arrangement::sequence (session, { a, b }));

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

TEST_CASE ("sequenceShared places one wrapper per offset, all sharing the same ChildPtr",
           "[arrangement][sequenceShared]")
{
    using ida::PhraseMetadata;

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (24)));
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextId = 50;
    auto allocate = [&nextId] { return ConstituentId (nextId++); };

    const Constituent arranged = ida::arrangement::sequenceShared (
        parent, verse,
        { Position (Rational (3)), Position (Rational (9)), Position (Rational (15)) },
        allocate);

    REQUIRE (arranged.children().size() == 3);

    // Each wrapper has wrapper-shape: role="placement", first child is the
    // shared verse, no TapeReference, conceptualIn/Out is [offset, offset+6).
    for (std::size_t i = 0; i < arranged.children().size(); ++i)
    {
        const auto& wrapper = *arranged.children()[i];
        REQUIRE (ida::isPlacementWrapper (wrapper));
        CHECK (wrapper.phraseMetadata()->role == "placement");
        CHECK_FALSE (wrapper.tapeReference().has_value());
        CHECK (wrapper.children().size() == 1);
    }

    // Pointer-identity equality — the canary that proves real sharing.
    CHECK (arranged.children()[0]->children()[0].get()
           == arranged.children()[1]->children()[0].get());
    CHECK (arranged.children()[1]->children()[0].get()
           == arranged.children()[2]->children()[0].get());

    // Wrapper ids minted in offset order from the allocator.
    CHECK (arranged.children()[0]->id().value() == 50);
    CHECK (arranged.children()[1]->id().value() == 51);
    CHECK (arranged.children()[2]->id().value() == 52);

    // Wrapper spans cover [offset, offset + verse->duration()).
    CHECK (arranged.children()[0]->conceptualIn()  == Position (Rational (3)));
    CHECK (arranged.children()[0]->conceptualOut() == Position (Rational (9)));
    CHECK (arranged.children()[1]->conceptualIn()  == Position (Rational (9)));
    CHECK (arranged.children()[1]->conceptualOut() == Position (Rational (15)));
    CHECK (arranged.children()[2]->conceptualIn()  == Position (Rational (15)));
    CHECK (arranged.children()[2]->conceptualOut() == Position (Rational (21)));
}

TEST_CASE ("sequenceShared rejects a null phrase and an empty offset list",
           "[arrangement][sequenceShared]")
{
    using ida::PhraseMetadata;

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (12)));
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    auto allocate = [] { return ConstituentId (99); };

    CHECK_THROWS_AS (
        ida::arrangement::sequenceShared (
            parent, nullptr,
            { Position (Rational (0)) }, allocate),
        std::invalid_argument);

    CHECK_THROWS_AS (
        ida::arrangement::sequenceShared (parent, verse, {}, allocate),
        std::invalid_argument);
}

TEST_CASE ("sequenceShared composes with the existing bare sequence",
           "[arrangement][sequenceShared]")
{
    using ida::PhraseMetadata;

    const Constituent parent (ConstituentId (1), Position(), Position (Rational (24)));

    const auto intro = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withPhraseMetadata (PhraseMetadata { .role = "intro" }));
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));
    const auto outro = std::make_shared<const Constituent> (
        Constituent (ConstituentId (30), Position(), Position (Rational (3)))
            .withPhraseMetadata (PhraseMetadata { .role = "outro" }));

    std::int64_t nextId = 50;
    auto allocate = [&nextId] { return ConstituentId (nextId++); };

    // intro (bare) + verse×3 (wrapped) + outro (bare).
    const Constituent withIntro = ida::arrangement::sequence (parent, { intro });
    const Constituent withVerses = ida::arrangement::sequenceShared (
        withIntro, verse,
        { Position (Rational (3)), Position (Rational (9)), Position (Rational (15)) },
        allocate);
    const Constituent full = ida::arrangement::sequence (
        withVerses, { outro });

    REQUIRE (full.children().size() == 5);
    CHECK (full.children()[0]->id().value() == 10);                       // intro
    CHECK (ida::isPlacementWrapper (*full.children()[1]));             // verse wrapper A
    CHECK (ida::isPlacementWrapper (*full.children()[2]));             // wrapper B
    CHECK (ida::isPlacementWrapper (*full.children()[3]));             // wrapper C
    // arrangement::sequence places its `outro` argument at childrenEnd, which
    // is the last wrapper's conceptualOut = Rational (21).
    CHECK (full.children()[4]->id().value() == 30);
    CHECK (full.children()[4]->conceptualIn()  == Position (Rational (21)));
    CHECK (full.children()[4]->conceptualOut() == Position (Rational (24)));
}
