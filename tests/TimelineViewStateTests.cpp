// Tests for the Preparation tab's timeline view selector (white paper Part
// 14.6; this session's UI design discussion, see continue.md). The selector
// is the JUCE-free half of Mockup A refined: a strip per InputDescriptor,
// a Pill per Phrase, both anchored to LMC seconds via the session's tempo
// map. These tests pin down the dominant single-tape case, the future-
// proofed multi-tape case (so we never regress the membership outline
// path), and the per-tape arm/focus plumbing that the bottom-bar Mark In
// gesture now relies on instead of TapeId{0}.
#include "ida/TimelineViewState.h"

#include "ida/Arrangement.h"
#include "ida/Constituent.h"
#include "ida/InputDescriptor.h"
#include "ida/InputKind.h"
#include "ida/Phrase.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/RepetitionRules.h"
#include "ida/TapeId.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using ida::Constituent;
using ida::ConstituentId;
using ida::EntranceCharacter;
using ida::ExitCharacter;
using ida::InputDescriptor;
using ida::InputKind;
using ida::PhraseMetadata;
using ida::Position;
using ida::Rational;
using ida::TapeId;
using ida::TapeReference;
using ida::TempoMap;

namespace
{
    std::shared_ptr<const Constituent> makeLoop (std::int64_t id,
                                                 const char* name,
                                                 Rational length,
                                                 std::int64_t tape)
    {
        const auto loop =
            Constituent (ConstituentId (id), Position(), Position (length))
                .withName (name)
                .withTapeReference (TapeReference (TapeId (tape),
                                                   Rational (0),
                                                   length));
        return std::make_shared<const Constituent> (loop);
    }

    InputDescriptor audioInput (std::int64_t tape, const char* name)
    {
        return InputDescriptor { TapeId (tape), InputKind::Audio, name, 0 };
    }
}

TEST_CASE ("empty session yields a timeline with the session's LMC bounds and "
           "no Pills", "[timelineview]")
{
    const Constituent session (ConstituentId (1), Position(), Position (Rational (4)));
    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)), {}, {}, TapeId (0));

    CHECK (state.startLmcSeconds == Rational (0));
    // 120 BPM: one whole note = 2 LMC seconds, so 4 wholeNotes = 8 LMC seconds.
    CHECK (state.endLmcSeconds == Rational (8));
    CHECK (state.rows.empty());
    CHECK (state.pills.empty());
}

TEST_CASE ("each InputDescriptor produces a row even when nothing references "
           "its tape", "[timelineview]")
{
    // An empty session still has rows for every configured input — an empty
    // track is a real input the performer hasn't captured into yet, and the
    // strip head still needs to show name, kind icon, arm state.
    const Constituent session (ConstituentId (1), Position(), Position (Rational (4)));
    const std::vector<InputDescriptor> inputs {
        audioInput (10, "Vocals"),
        audioInput (20, "Guitar")
    };
    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)), inputs, {}, TapeId (0));

    REQUIRE (state.rows.size() == 2);
    CHECK (state.rows[0].tapeId == TapeId (10));
    CHECK (state.rows[0].displayName == "Vocals");
    CHECK (state.rows[0].kind == InputKind::Audio);
    CHECK_FALSE (state.rows[0].isArmed);
    CHECK_FALSE (state.rows[0].isFocused);
    CHECK (state.rows[1].tapeId == TapeId (20));
}

TEST_CASE ("a single-tape Phrase becomes one Pill anchored to its lone tape",
           "[timelineview]")
{
    // The dominant case: one performer, one input, one phrase containing one
    // loop. The Pill's primary tape is unambiguous; the membership vector is
    // a single-entry list. This is the case Mockup A is optimised for.
    PhraseMetadata pm;
    pm.role     = "verse";
    pm.entrance = EntranceCharacter::Pickup;
    pm.exit     = ExitCharacter::Resolution;
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (pm)
            .withChildAdded (makeLoop (21, "verse: rhythm", Rational (4), 200)));

    const Constituent session =
        Constituent (ConstituentId (1), Position(), Position (Rational (4)))
            .withName ("session")
            .withChildAdded (verse);

    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)),
        { audioInput (200, "Vocals") }, {}, TapeId (0));

    REQUIRE (state.pills.size() == 1);
    const auto& pill = state.pills[0];
    CHECK (pill.id == ConstituentId (20));
    CHECK (pill.name == "verse");
    CHECK (pill.startLmcSeconds == Rational (0));
    CHECK (pill.endLmcSeconds == Rational (8));
    CHECK (pill.primaryTape == TapeId (200));
    REQUIRE (pill.memberTapes.size() == 1);
    CHECK (pill.memberTapes[0] == TapeId (200));
    CHECK (pill.loopCount == 1);
    CHECK (pill.entranceName == "pickup");
    CHECK (pill.exitName == "resolution");
}

TEST_CASE ("a multi-tape Phrase picks the most-referenced tape as primary and "
           "exposes the full membership in insertion order", "[timelineview]")
{
    // The future-proofed multi-tape case: a Phrase containing loops on two
    // tapes, with the second tape referenced twice. Primary is the mode;
    // member tapes preserve insertion order so the renderer's membership
    // outline draws predictably rather than in hash order.
    PhraseMetadata pm;
    pm.role = "chorus";
    Constituent chorusShell =
        Constituent (ConstituentId (40), Position(), Position (Rational (4)))
            .withName ("chorus")
            .withPhraseMetadata (pm);

    const auto chorus = std::make_shared<const Constituent> (
        ida::arrangement::layer (chorusShell, {
            makeLoop (41, "chorus: vocals", Rational (4), 100),
            makeLoop (42, "chorus: gtr1",   Rational (4), 200),
            makeLoop (43, "chorus: gtr2",   Rational (4), 200)
        }));

    const Constituent session =
        Constituent (ConstituentId (1), Position(), Position (Rational (4)))
            .withName ("session")
            .withChildAdded (chorus);

    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)),
        { audioInput (100, "Vocals"), audioInput (200, "Guitar") },
        {}, TapeId (0));

    REQUIRE (state.pills.size() == 1);
    const auto& pill = state.pills[0];
    CHECK (pill.loopCount == 3);
    CHECK (pill.primaryTape == TapeId (200));  // referenced twice
    REQUIRE (pill.memberTapes.size() == 2);
    CHECK (pill.memberTapes[0] == TapeId (100)); // insertion order: vocals first
    CHECK (pill.memberTapes[1] == TapeId (200));
}

TEST_CASE ("armed and focused tapes propagate to their rows", "[timelineview]")
{
    // The per-tape arm gesture is the primary capture gesture in the refined
    // Mockup A; the focused tape is what the bottom-bar Mark In targets, so
    // both flags must round-trip through the selector verbatim.
    const Constituent session (ConstituentId (1), Position(), Position (Rational (4)));
    const std::vector<InputDescriptor> inputs {
        audioInput (10, "Vocals"),
        audioInput (20, "Guitar"),
        audioInput (30, "Keys")
    };
    const std::vector<TapeId> armed { TapeId (10), TapeId (30) };

    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)),
        inputs, armed, TapeId (30));

    REQUIRE (state.rows.size() == 3);
    CHECK (state.rows[0].isArmed);
    CHECK_FALSE (state.rows[0].isFocused);
    CHECK_FALSE (state.rows[1].isArmed);
    CHECK_FALSE (state.rows[1].isFocused);
    CHECK (state.rows[2].isArmed);
    CHECK (state.rows[2].isFocused);
}

TEST_CASE ("a Phrase's span is its parent-conceptual position through the "
           "session tempo map, not its tape slice", "[timelineview]")
{
    // The Pill's start and end on the timeline come from the song-structure
    // position (conceptualIn/Out × TempoMap), independent of where on the
    // tape its loops happen to read from. Pin this down so a refactor of the
    // walk function never silently substitutes one for the other.
    PhraseMetadata pm;
    const auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (2)))
            .withName ("verse")
            .withPhraseMetadata (pm)
            // The loop reads from tape seconds 5.0 → 7.0 — completely
            // unrelated to where on the timeline the Pill is drawn.
            .withChildAdded (
                std::make_shared<const Constituent> (
                    Constituent (ConstituentId (21), Position(), Position (Rational (2)))
                        .withName ("loop")
                        .withTapeReference (TapeReference (TapeId (200),
                                                           Rational (5),
                                                           Rational (7))))));

    const Constituent session = ida::arrangement::sequence (
        Constituent (ConstituentId (1), Position(), Position (Rational (6)))
            .withName ("session"),
        { makeLoop (10, "intro", Rational (4), 100), verse });

    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)),
        { audioInput (100, "A"), audioInput (200, "B") },
        {}, TapeId (0));

    REQUIRE (state.pills.size() == 1);
    // intro occupies whole notes [0, 4) = LMC [0, 8); verse begins at 4
    // wholeNotes = LMC 8 and runs 2 wholeNotes = 4 LMC seconds.
    CHECK (state.pills[0].startLmcSeconds == Rational (8));
    CHECK (state.pills[0].endLmcSeconds == Rational (12));
}

TEST_CASE ("a Phrase with cardinality::Once reports phraseLoopActive = false",
           "[timelineview]")
{
    // The Pill's top-right ↻ atom toggles with cardinality: Once is a single
    // play (off), everything else (Forever, NTimes, Until*) is some flavour
    // of looping (on). Verifies the cardinality → UI mapping is honest.
    PhraseMetadata pm;
    ida::RepetitionRules once;
    once.cardinality = ida::cardinality::Once{};

    const auto phraseOnce = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (2)))
            .withName ("oneshot")
            .withPhraseMetadata (pm)
            .withRepetitionRules (once));

    const Constituent session =
        Constituent (ConstituentId (1), Position(), Position (Rational (2)))
            .withChildAdded (phraseOnce);

    const auto state = ida::selectTimelineView (
        session, TempoMap::fromBpm (Rational (120)), {}, {}, TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK_FALSE (state.pills[0].phraseLoopActive);
}

TEST_CASE ("selectTimelineView emits one Pill per wrapper, content delegated to shared Phrase",
           "[timelineView][shared]")
{
    auto verse = std::make_shared<const Constituent> (
        ida::arrangement::layer (
            Constituent (ConstituentId (20), Position(), Position (Rational (4)))
                .withName ("verse")
                .withPhraseMetadata (PhraseMetadata { .role = "verse",
                                                       .entrance = EntranceCharacter::Downbeat,
                                                       .exit     = ExitCharacter::HandOff }),
            { makeLoop (21, "verse: rhythm", Rational (4), 200) }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (12)));
    const Constituent root = ida::arrangement::sequenceShared (
        shell, verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const std::vector<InputDescriptor> inputs {
        { TapeId (200), InputKind::Audio, "Rhythm", 0 } };

    const auto state = ida::selectTimelineView (
        root, identity, inputs, /*armed*/ {}, /*focused*/ TapeId (200));

    // Three Pills, one per wrapper. Shared verse itself is suppressed.
    REQUIRE (state.pills.size() == 3);
    for (std::size_t i = 0; i < state.pills.size(); ++i)
    {
        const auto& pill = state.pills[i];
        // Pill id is the WRAPPER's id, not the shared Phrase's id.
        CHECK (pill.id.value() == static_cast<std::int64_t> (50 + i));
        // Pill content (loop count, primary tape, name, entrance/exit) comes
        // from the shared verse, not from the wrapper itself.
        CHECK (pill.name        == "verse");
        CHECK (pill.loopCount   == 1);
        CHECK (pill.primaryTape == TapeId (200));
        CHECK (pill.entranceName == "downbeat");
        CHECK (pill.exitName     == "hand-off");
    }
}

TEST_CASE ("selectTimelineView populates sharedSiblings via pointer-identity grouping",
           "[timelineView][shared]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    std::int64_t nextWrapperId = 50;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (12)));
    const Constituent root = ida::arrangement::sequenceShared (
        shell, verse,
        { Position (Rational (0)), Position (Rational (4)), Position (Rational (8)) },
        allocateWrapper);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 3);

    // Pill A (id 50) shares with B (51) and C (52).
    CHECK (state.pills[0].sharedSiblings.size() == 2);
    CHECK (state.pills[1].sharedSiblings.size() == 2);
    CHECK (state.pills[2].sharedSiblings.size() == 2);

    auto containsId = [] (const std::vector<ConstituentId>& v, std::int64_t want)
    {
        for (const auto& id : v) if (id.value() == want) return true;
        return false;
    };
    CHECK (containsId (state.pills[0].sharedSiblings, 51));
    CHECK (containsId (state.pills[0].sharedSiblings, 52));
}

TEST_CASE ("selectTimelineView leaves sharedSiblings empty for bare Phrases",
           "[timelineView][bare]")
{
    const Constituent shell (ConstituentId (1), Position(), Position (Rational (6)));
    auto intro = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withName ("intro")
            .withPhraseMetadata (PhraseMetadata { .role = "intro" }));
    const Constituent root = shell.withChildAdded (intro);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK (state.pills[0].sharedSiblings.empty());
    CHECK_FALSE (state.pills[0].hasOverlays);
    CHECK_FALSE (state.pills[0].isForked);
}

TEST_CASE ("selectTimelineView sets hasOverlays when a wrapper has overlay Loops",
           "[timelineView][overlay]")
{
    auto verse = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));
    auto overlay = std::make_shared<const Constituent> (
        Constituent (ConstituentId (60), Position(), Position (Rational (4)))
            .withTapeReference (TapeReference (TapeId (200), Rational (0), Rational (4))));

    // One wrapper, manually built so it has both the shared verse AND an overlay.
    PhraseMetadata wrapperMeta;
    wrapperMeta.role = "placement";
    const auto wrapper = std::make_shared<const Constituent> (
        Constituent (ConstituentId (50), Position(), Position (Rational (4)))
            .withPhraseMetadata (wrapperMeta)
            .withChildAdded (verse)
            .withChildAdded (overlay));

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (4)));
    const Constituent root = shell.withChildAdded (wrapper);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK (state.pills[0].id.value() == 50);
    CHECK (state.pills[0].hasOverlays);
}

TEST_CASE ("selectTimelineView sets isForked when wrapper role is 'forked-placement'",
           "[timelineView][forked]")
{
    auto versePhrase = std::make_shared<const Constituent> (
        Constituent (ConstituentId (200), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (PhraseMetadata { .role = "verse" }));

    PhraseMetadata forkedMeta;
    forkedMeta.role = "forked-placement";
    const auto forked = std::make_shared<const Constituent> (
        Constituent (ConstituentId (50), Position(), Position (Rational (4)))
            .withPhraseMetadata (forkedMeta)
            .withChildAdded (versePhrase));

    const Constituent shell (ConstituentId (1), Position(), Position (Rational (4)));
    const Constituent root = shell.withChildAdded (forked);

    const TempoMap identity = TempoMap::fromBpm (Rational (120));
    const auto state = ida::selectTimelineView (
        root, identity, /*inputs*/ {}, /*armed*/ {}, /*focused*/ TapeId (0));

    REQUIRE (state.pills.size() == 1);
    CHECK (state.pills[0].isForked);
    CHECK (state.pills[0].sharedSiblings.empty());
}
