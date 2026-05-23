// Tests for the session JSON format (white paper Part 7.8). The single load-
// bearing property here is round-trip fidelity: a serialized session, parsed
// and re-serialized, must produce identical JSON. That property covers every
// field of a Constituent — id, boundaries, anchor, name, local meter and tempo
// map, all five repetition-rule variants, phrase metadata, tape references,
// and children — without us having to compare each one by hand. Negative tests
// pin down that corruption fails loud (white paper Part 13.3, rule 3 — never
// silent).
#include "ida/Arrangement.h"
#include "ida/Constituent.h"
#include "ida/ConstituentId.h"
#include "ida/Meter.h"
#include "ida/MixerGraphState.h"
#include "ida/Phrase.h"
#include "ida/Position.h"
#include "ida/Promotion.h"
#include "ida/Rational.h"
#include "ida/RepetitionRules.h"
#include "ida/SessionFormat.h"
#include "ida/TapeId.h"
#include "ida/TapeReference.h"
#include "ida/TempoMap.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <memory>
#include <stdexcept>
#include <variant>

using ida::AnchorToParent;
using ida::Constituent;
using ida::ConstituentId;
using ida::Meter;
using ida::Mutation;
using ida::Position;
using ida::Rational;
using ida::RepetitionRules;
using ida::TapeId;
using ida::TapeReference;
using ida::TempoMap;

namespace
{
    /// Builds a Constituent tree exercising every field the serializer touches:
    /// non-default boundaries and anchor, a local meter and tempo map, every
    /// repetition-rule dimension set to a non-default value with parameters,
    /// phrase metadata with a grammatical link, a tape reference, and a layered
    /// pair of children.
    std::shared_ptr<const Constituent> exhaustiveTree()
    {
        RepetitionRules rules;
        rules.trigger     = ida::trigger::EveryNBars (4);
        rules.cardinality = ida::cardinality::NTimes (8);
        rules.phase       = ida::phase::QuantizedToGrid (Rational (1, 4));
        rules.mutation    = Mutation::Decaying;
        rules.termination = ida::termination::FadeOverBars (Rational (2));

        ida::PhraseMetadata phrase;
        phrase.role = "verse";
        phrase.intent = "introduce the harmonic centre";
        phrase.entrance = ida::EntranceCharacter::Pickup;
        phrase.exit = ida::ExitCharacter::HandOff;
        phrase.isRoleFillable = true;
        phrase.grammaticalLinks.push_back (
            { ida::GrammaticalLink::Kind::CallAndResponse, ConstituentId (99) });

        const Constituent loop =
            Constituent (ConstituentId (10), Position(), Position (Rational (4)))
                .withName ("intro loop")
                .withTapeReference (TapeReference (TapeId (200), Rational (1), Rational (5)));

        Constituent verse =
            Constituent (ConstituentId (20), Position(), Position (Rational (8)))
                .withName ("verse")
                .withAnchor (AnchorToParent::Start)
                .withLocalMeter (Meter (7, 8))
                .withLocalTempoMap (TempoMap::fromBpm (Rational (132)))
                .withRepetitionRules (rules)
                .withPhraseMetadata (phrase);

        verse = ida::arrangement::layer (verse,
            { std::make_shared<const Constituent> (loop),
              std::make_shared<const Constituent> (
                  Constituent (ConstituentId (11), Position(), Position (Rational (4)))
                      .withName ("intro layer")) });

        const Constituent root =
            Constituent (ConstituentId (1), Position(), Position (Rational (12)))
                .withName ("demo session");
        return std::make_shared<const Constituent> (
            ida::arrangement::sequence (root, { std::make_shared<const Constituent> (verse) }));
    }
}

TEST_CASE ("a session round-trips through JSON byte-for-byte", "[sessionformat]")
{
    const auto original = exhaustiveTree();
    const auto firstPass = ida::persistence::serializeSession (*original);
    const auto reconstructed = ida::persistence::deserializeSession (firstPass);
    const auto secondPass = ida::persistence::serializeSession (*reconstructed);
    CHECK (firstPass == secondPass);
}

TEST_CASE ("round-trip preserves the structural claims of the data model",
           "[sessionformat]")
{
    // Spot-check the fields that carry the white paper's load-bearing claims:
    // identity is preserved (id survives), the tree shape survives, and each
    // variant dimension carries its parameters across.
    const auto original = exhaustiveTree();
    const auto json = ida::persistence::serializeSession (*original);
    const auto round = ida::persistence::deserializeSession (json);

    CHECK (round->id() == ConstituentId (1));
    REQUIRE (round->children().size() == 1);

    const auto& verse = *round->children()[0];
    CHECK (verse.name() == "verse");
    CHECK (verse.anchor() == AnchorToParent::Start);
    REQUIRE (verse.localMeter().has_value());
    CHECK (verse.localMeter().value() == Meter (7, 8));
    REQUIRE (verse.localTempoMap().has_value());

    const auto& rules = verse.repetitionRules();
    CHECK (std::get<ida::trigger::EveryNBars> (rules.trigger).bars == 4);
    CHECK (std::get<ida::cardinality::NTimes> (rules.cardinality).count == 8);
    CHECK (std::get<ida::phase::QuantizedToGrid> (rules.phase).division == Rational (1, 4));
    CHECK (rules.mutation == Mutation::Decaying);
    CHECK (std::get<ida::termination::FadeOverBars> (rules.termination).bars == Rational (2));

    REQUIRE (verse.phraseMetadata().has_value());
    CHECK (verse.phraseMetadata()->role == "verse");
    CHECK (verse.phraseMetadata()->isRoleFillable);
    REQUIRE (verse.phraseMetadata()->grammaticalLinks.size() == 1);
    CHECK (verse.phraseMetadata()->grammaticalLinks[0].target == ConstituentId (99));

    REQUIRE (verse.children().size() == 2);
    const auto& loop = *verse.children()[0];
    REQUIRE (loop.tapeReference().has_value());
    CHECK (loop.tapeReference()->tape == TapeId (200));
    CHECK (loop.tapeReference()->tapeIn == Rational (1));
    CHECK (loop.tapeReference()->tapeOut == Rational (5));
}

TEST_CASE ("a minimal default Constituent round-trips", "[sessionformat]")
{
    // Default-only fields exercise the serializer's "absent optional" handling
    // and confirm the defaults survive the trip — the freshly-loaded session
    // must look identical to a freshly-constructed one.
    const Constituent c (ConstituentId (1), Position(), Position (Rational (4)));
    const auto json = ida::persistence::serializeSession (c);
    const auto round = ida::persistence::deserializeSession (json);

    CHECK (round->id() == ConstituentId (1));
    CHECK (round->name().empty());
    CHECK (round->anchor() == AnchorToParent::Free);
    CHECK_FALSE (round->localMeter().has_value());
    CHECK_FALSE (round->localTempoMap().has_value());
    CHECK_FALSE (round->phraseMetadata().has_value());
    CHECK_FALSE (round->tapeReference().has_value());
    CHECK (round->children().empty());
}

TEST_CASE ("malformed JSON is rejected with a hard error", "[sessionformat]")
{
    // Rule 3: degradation is announced, not silent. A corrupt session must
    // throw, never silently return a degraded tree.
    CHECK_THROWS_AS (ida::persistence::deserializeSession ("{not json}"),
                     std::runtime_error);
    CHECK_THROWS_AS (ida::persistence::deserializeSession ("[1,2,3]"),
                     std::runtime_error);
    CHECK_THROWS_AS (ida::persistence::deserializeSession ("{}"),
                     std::runtime_error);
}

TEST_CASE ("an unsupported version is rejected", "[sessionformat]")
{
    CHECK_THROWS_AS (
        ida::persistence::deserializeSession (R"({ "version": 99, "root": {} })"),
        std::runtime_error);
}

TEST_CASE ("an unknown variant tag is rejected", "[sessionformat]")
{
    // Start from a valid document, then poison one variant kind. This catches
    // both forward-compat unknowns and outright corruption.
    const Constituent c (ConstituentId (1), Position(), Position (Rational (1)));
    auto json = ida::persistence::serializeSession (c);
    json = json.replace ("FreeRunning", "SomethingWeird");
    CHECK_THROWS_AS (ida::persistence::deserializeSession (json),
                     std::runtime_error);
}

namespace
{
    /// Mirrors the demo session's verse × N shape (DemoSession.cpp): a song
    /// shell holding `placements` wrapper Phrases that all point at one
    /// shared verse ChildPtr. The test uses this to assert that round-trip
    /// preserves the wrapper-children-are-pointer-equal invariant that the
    /// in-memory model relies on.
    std::shared_ptr<const Constituent> sharedVerseTree (int placements)
    {
        ida::PhraseMetadata verseMeta;
        verseMeta.role = "verse";
        const Constituent verseShell =
            Constituent (ConstituentId (20), Position(), Position (Rational (6)))
                .withName ("verse")
                .withPhraseMetadata (verseMeta);
        const auto verse = std::make_shared<const Constituent> (
            ida::arrangement::layer (verseShell,
                { std::make_shared<const Constituent> (
                    Constituent (ConstituentId (21), Position(), Position (Rational (6)))
                        .withName ("verse: rhythm")
                        .withTapeReference (
                            TapeReference (TapeId (200), Rational (0), Rational (12)))) }));

        std::vector<Position> offsets;
        for (int i = 0; i < placements; ++i)
            offsets.push_back (Position (Rational (i * 6)));

        std::int64_t nextId = 51;
        auto allocate = [&nextId] { return ConstituentId (nextId++); };

        const Constituent songShell =
            Constituent (ConstituentId (1), Position(), Position (Rational (6 * placements)))
                .withName ("test song");
        return std::make_shared<const Constituent> (
            ida::arrangement::sequenceShared (songShell, verse, offsets, allocate));
    }
}

TEST_CASE ("a shared placement round-trips with pointer-identity preserved",
           "[sessionformat][sharing]")
{
    // The load-bearing property of v2: serialize the verse × 3 shape, parse
    // it back, and the three wrappers must still hold the SAME ChildPtr to
    // the verse Phrase. This is what an edit to one verse continues to
    // propagate to its siblings across a save / load round-trip.
    const auto original = sharedVerseTree (3);
    const auto json = ida::persistence::serializeSession (*original);
    const auto round = ida::persistence::deserializeSession (json);

    REQUIRE (round->children().size() == 3);
    const auto wrapperZeroChild = round->children()[0]->children()[0].get();
    const auto wrapperOneChild  = round->children()[1]->children()[0].get();
    const auto wrapperTwoChild  = round->children()[2]->children()[0].get();
    CHECK (wrapperZeroChild == wrapperOneChild);
    CHECK (wrapperZeroChild == wrapperTwoChild);
    CHECK (wrapperZeroChild->id() == ConstituentId (20));
}

TEST_CASE ("the loaded shared tree passes the shared-instance guard",
           "[sessionformat][sharing]")
{
    // deserializeSession runs the guard internally; this test pins that
    // contract: a freshly-loaded tree is immediately legal as the argument
    // to promotion::promote (which calls the same guard first thing).
    const auto original = sharedVerseTree (3);
    const auto json = ida::persistence::serializeSession (*original);
    const auto round = ida::persistence::deserializeSession (json);
    CHECK_NOTHROW (ida::promotion::enforceSharedInstancesAreShared (*round));
}

TEST_CASE ("a v2 document is rejected if a ref precedes the def",
           "[sessionformat][sharing]")
{
    // Hand-crafted: the children array references id 20 before any constituent
    // with that id has been emitted. Forward refs are not supported (the
    // serializer always emits the def first in document order), and a corrupt
    // document that swaps the order must fail loud rather than load a
    // dangling pointer.
    const auto json = juce::String (R"({
        "version": 2,
        "root": {
            "id": 1, "in": "0/1", "out": "12/1", "anchor": "Free", "name": "song",
            "rules": { "trigger": { "kind": "FreeRunning" },
                       "cardinality": { "kind": "Forever" },
                       "phase": { "kind": "Free" },
                       "mutation": "Identical",
                       "termination": { "kind": "CompleteCurrentCycle" } },
            "children": [
                { "ref": 20 }
            ]
        }
    })");
    CHECK_THROWS_AS (ida::persistence::deserializeSession (json),
                     std::runtime_error);
}

TEST_CASE ("a v1 document is rejected by the v2 loader",
           "[sessionformat][sharing]")
{
    // v1 sessions emitted each shared Phrase multiple times (no refs) and
    // would deserialize into duplicate-id distinct allocations. The v2 loader
    // refuses them outright rather than try to migrate; the user re-saves.
    CHECK_THROWS_AS (
        ida::persistence::deserializeSession (R"({ "version": 1, "root": {} })"),
        std::runtime_error);
}

TEST_CASE ("a shared session round-trips through an actual file on disk",
           "[sessionformat][sharing][disk]")
{
    // The in-memory round-trip cases above prove serializeSession /
    // deserializeSession agree about the JSON shape. This case adds the
    // juce::File wrappers (replaceWithText / loadFileAsString) used by
    // MainComponent::chooseFileAndSave / chooseFileAndLoad, so the full save
    // / load path the operator uses is covered without launching the .app.
    const auto original = sharedVerseTree (3);
    const auto json = ida::persistence::serializeSession (*original);

    auto tmp = juce::File::createTempFile (".ida.json");
    REQUIRE (tmp.replaceWithText (json));

    const auto loadedJson = tmp.loadFileAsString();
    REQUIRE (loadedJson == json);

    const auto round = ida::persistence::deserializeSession (loadedJson);
    tmp.deleteFile();

    REQUIRE (round->children().size() == 3);
    const auto a = round->children()[0]->children()[0].get();
    const auto b = round->children()[1]->children()[0].get();
    const auto c = round->children()[2]->children()[0].get();
    CHECK (a == b);
    CHECK (a == c);
    CHECK (a->id() == ConstituentId (20));
}

TEST_CASE ("a v2 document with two full objects sharing an id fails loud",
           "[sessionformat][sharing]")
{
    // Constructed-by-hand corrupt v2: the children array contains two full
    // constituent objects, both with id 20. A correct v2 emitter would have
    // used a ref for the second occurrence. The post-load shared-instance
    // guard catches this — two distinct allocations carry the same id, which
    // violates the pointer-identity contract promote() relies on.
    const auto json = juce::String (R"({
        "version": 2,
        "root": {
            "id": 1, "in": "0/1", "out": "12/1", "anchor": "Free", "name": "song",
            "rules": { "trigger": { "kind": "FreeRunning" },
                       "cardinality": { "kind": "Forever" },
                       "phase": { "kind": "Free" },
                       "mutation": "Identical",
                       "termination": { "kind": "CompleteCurrentCycle" } },
            "children": [
                { "id": 20, "in": "0/1", "out": "6/1", "anchor": "Free", "name": "verse",
                  "rules": { "trigger": { "kind": "FreeRunning" },
                             "cardinality": { "kind": "Forever" },
                             "phase": { "kind": "Free" },
                             "mutation": "Identical",
                             "termination": { "kind": "CompleteCurrentCycle" } },
                  "children": [] },
                { "id": 20, "in": "0/1", "out": "6/1", "anchor": "Free", "name": "verse",
                  "rules": { "trigger": { "kind": "FreeRunning" },
                             "cardinality": { "kind": "Forever" },
                             "phase": { "kind": "Free" },
                             "mutation": "Identical",
                             "termination": { "kind": "CompleteCurrentCycle" } },
                  "children": [] }
            ]
        }
    })");
    CHECK_THROWS_AS (ida::persistence::deserializeSession (json),
                     std::logic_error);
}

namespace
{
    ida::InputMixerGraphState sampleInputState()
    {
        using namespace sirius;
        InputMixerGraphState s;
        MixerBusState bus; bus.busId = 1; bus.channelCount = 2; bus.name = "Drums";
        bus.kind = MixerBusKind::Bus;
        bus.mainOut.kind = MixerMainOut::Kind::Terminal; bus.mainOut.terminal = MixerTerminalKind::Tape;
        auto comp = EffectChainEntry::makePlugin (PluginDescriptor{}, "comp", "");
        comp.bypassed = true;
        bus.inserts = EffectChain{}.withAppended (comp);
        s.buses.push_back (bus);
        MixerBusState rev; rev.busId = 2; rev.name = "Reverb"; rev.kind = MixerBusKind::FxReturn;
        rev.mainOut.kind = MixerMainOut::Kind::Terminal; rev.mainOut.terminal = MixerTerminalKind::Tape;
        s.buses.push_back (rev);
        InputChannelState ch; ch.channelId = 5; ch.signalType = SignalType::Audio; ch.inputSourceId = 1;
        ch.source = { 2, 3, true }; ch.tapeMode = TapeMode::CommitToTape;
        ch.mainOut.kind = MixerMainOut::Kind::Bus; ch.mainOut.busId = 1;
        ch.sends.push_back ({ 2, 0.5f });
        const auto eq = EffectChainEntry::makePlugin (PluginDescriptor{}, "eq", "");
        ch.inserts = EffectChain{}.withAppended (eq);
        s.channels.push_back (ch);
        s.nextBusId = 3; s.nextChannelId = 6;
        return s;
    }
}

TEST_CASE ("InputMixerGraphState round-trips through JSON", "[sessionformat]")
{
    const auto original = sampleInputState();
    const auto json     = ida::persistence::serializeMixerGraphState (original);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);
    CHECK (restored == original);
    // Byte-stable: serialize -> deserialize -> serialize yields identical JSON.
    CHECK (ida::persistence::serializeMixerGraphState (restored) == json);
}

TEST_CASE ("OutputMixerGraphState round-trips through JSON", "[sessionformat]")
{
    using namespace sirius;
    OutputMixerGraphState s;
    MixerBusState master; master.busId = 0; master.name = "Master";
    master.mainOut.kind = MixerMainOut::Kind::Terminal; master.mainOut.terminal = MixerTerminalKind::HardwareOutput;
    s.buses.push_back (master);
    OutputChannelState ch; ch.channelId = 1; ch.sends.push_back ({ 0, 1.0f });
    s.channels.push_back (ch);
    s.nextBusId = 1; s.nextChannelId = 2;

    const auto json     = ida::persistence::serializeMixerGraphState (s);
    const auto restored = ida::persistence::deserializeOutputMixerGraphState (json);
    CHECK (restored == s);
}

TEST_CASE ("a pre-graph (empty) mixer document deserializes to defaults", "[sessionformat]")
{
    // A document carrying only a version and empty arrays — what a forward
    // session that never populated the graph would write.
    const ida::InputMixerGraphState empty;
    const auto json     = ida::persistence::serializeMixerGraphState (empty);
    const auto restored = ida::persistence::deserializeInputMixerGraphState (json);
    CHECK (restored.buses.empty());
    CHECK (restored.channels.empty());
    CHECK (restored.nextBusId == 1);
    CHECK (restored.nextChannelId == 1);
}

TEST_CASE ("malformed mixer-graph JSON is rejected with a hard error", "[sessionformat]")
{
    CHECK_THROWS_AS (ida::persistence::deserializeInputMixerGraphState ("{not json}"),
                     std::runtime_error);
    CHECK_THROWS_AS (ida::persistence::deserializeInputMixerGraphState ("[1,2,3]"),
                     std::runtime_error);
}

TEST_CASE ("a pre-graph mixer document missing all graph keys loads as defaults", "[sessionformat]")
{
    // A forward-compat document: version present, but none of the graph keys —
    // exactly what a session predating the routing graph would carry. The
    // optionalProperty reads must default everything to an empty graph.
    auto obj = juce::DynamicObject::Ptr { new juce::DynamicObject() };
    obj->setProperty ("version", 1);
    const auto json = juce::JSON::toString (juce::var (obj.get()));

    const auto in = ida::persistence::deserializeInputMixerGraphState (json);
    CHECK (in.buses.empty());
    CHECK (in.channels.empty());
    CHECK (in.nextBusId == 1);
    CHECK (in.nextChannelId == 1);

    const auto out = ida::persistence::deserializeOutputMixerGraphState (json);
    CHECK (out.buses.empty());
    CHECK (out.channels.empty());
    CHECK (out.nextBusId == 1);
    CHECK (out.nextChannelId == 1);
}

TEST_CASE ("TapePool round-trips through serialize/deserialize", "[sessionformat][tape-pool]")
{
    ida::TapePool pool;
    const auto drums = pool.add ("Drums");
    pool.add ("Vox");
    pool.rename (drums, "Kit");

    const auto restored = ida::persistence::deserializeTapePool (
        ida::persistence::serializeTapePool (pool));
    REQUIRE (restored.count() == pool.count());
    CHECK (restored.tapes() == pool.tapes());
    CHECK (restored.primary() == pool.primary());
}

TEST_CASE ("a malformed tape-pool document throws", "[sessionformat][tape-pool]")
{
    // deserializeTapePool must throw on a missing 'tapes' key — back-compat is
    // the caller's responsibility; the function does not silently default.
    CHECK_THROWS_AS (ida::persistence::deserializeTapePool ("{}"),
                     std::runtime_error);
    CHECK_THROWS_AS (ida::persistence::deserializeTapePool ("{not json}"),
                     std::runtime_error);
}
