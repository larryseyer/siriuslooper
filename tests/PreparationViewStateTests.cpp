// Tests for the Preparation view's state selector (white paper Part 14.6). The
// Preparation view is the dense, allowed-to-look surface, so these tests pin
// down that the selector enumerates every Constituent in the tree with the
// right indent, kind, and flags — and that role-fillable phrases (8.4) and
// effect chains (7.7) show up where the performer will look for them.
#include "sirius/PreparationViewState.h"

#include "sirius/Constituent.h"
#include "sirius/EffectChain.h"
#include "sirius/Phrase.h"
#include "sirius/PluginDescriptor.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/TapeId.h"
#include "sirius/TapeReference.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::EffectChain;
using sirius::EffectChainEntry;
using sirius::PhraseMetadata;
using sirius::PluginFormat;
using sirius::Position;
using sirius::Rational;
using sirius::TapeId;
using sirius::TapeReference;

TEST_CASE ("a single Constituent appears as one row at indent 0", "[prepview]")
{
    const Constituent solo (ConstituentId (1), Position(), Position (Rational (4)));
    const auto state = sirius::selectPreparationView (solo);
    REQUIRE (state.rows.size() == 1);
    CHECK (state.rows[0].indentLevel == 0);
    CHECK (state.rows[0].id == ConstituentId (1));
    CHECK (state.rows[0].kind == std::string ("group"));
    CHECK (state.rows[0].durationWholeNotes == Rational (4));
}

TEST_CASE ("a nested tree appears depth-first with rising indent", "[prepview]")
{
    const auto leaf = std::make_shared<const Constituent> (
        Constituent (ConstituentId (10), Position(), Position (Rational (2)))
            .withName ("inner")
            .withTapeReference (TapeReference (TapeId (1), Rational (0), Rational (1))));

    PhraseMetadata pm;
    pm.role = "verse";
    pm.isRoleFillable = true;
    const auto phrase = std::make_shared<const Constituent> (
        Constituent (ConstituentId (20), Position(), Position (Rational (4)))
            .withName ("verse")
            .withPhraseMetadata (pm)
            .withChildAdded (leaf));

    EffectChainEntry e;
    e.descriptor.format = PluginFormat::Vst3;
    e.descriptor.uniqueId = "EQ-1";
    e.descriptor.name = "EQ";
    e.descriptor.manufacturer = "Acme";
    e.displayName = "EQ";

    const Constituent root =
        Constituent (ConstituentId (1), Position(), Position (Rational (8)))
            .withName ("session")
            .withEffectChain (EffectChain().withAppended (e))
            .withChildAdded (phrase);

    const auto state = sirius::selectPreparationView (root);
    REQUIRE (state.rows.size() == 3);

    CHECK (state.rows[0].indentLevel == 0);
    CHECK (state.rows[0].name == "session");
    CHECK (state.rows[0].hasEffectChain);
    CHECK (state.rows[0].effectCount == 1);

    CHECK (state.rows[1].indentLevel == 1);
    CHECK (state.rows[1].name == "verse");
    CHECK (state.rows[1].kind == std::string ("phrase"));
    CHECK (state.rows[1].isRoleFillable);

    CHECK (state.rows[2].indentLevel == 2);
    CHECK (state.rows[2].name == "inner");
    CHECK (state.rows[2].kind == std::string ("loop"));
}
