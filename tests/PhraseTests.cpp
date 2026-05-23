// Tests for ida::PhraseMetadata — the metadata a phrase carries beyond what
// a loop carries (white paper Part VIII). Phrase metadata is plain data with no
// invariants; these tests pin down its defaults and that its open-ended fields
// hold what they are given.
#include "sirius/Phrase.h"

#include <catch2/catch_test_macros.hpp>

using ida::ConstituentId;
using ida::EntranceCharacter;
using ida::ExitCharacter;
using ida::GrammaticalLink;
using ida::PhraseMetadata;

TEST_CASE ("phrase metadata starts blank and unspecified", "[phrase]")
{
    const PhraseMetadata meta;
    CHECK (meta.role.empty());
    CHECK (meta.intent.empty());
    CHECK (meta.entrance == EntranceCharacter::Unspecified);
    CHECK (meta.exit == ExitCharacter::Unspecified);
    CHECK (meta.grammaticalLinks.empty());
    CHECK_FALSE (meta.isRoleFillable);
}

TEST_CASE ("a phrase carries role, intent, and entrance/exit character", "[phrase]")
{
    PhraseMetadata meta;
    meta.role = "chorus";
    meta.intent = "the emotional peak — lands hard after the build";
    meta.entrance = EntranceCharacter::Pickup;
    meta.exit = ExitCharacter::HandOff;
    meta.isRoleFillable = true;

    CHECK (meta.role == "chorus");
    CHECK (meta.intent == "the emotional peak — lands hard after the build");
    CHECK (meta.entrance == EntranceCharacter::Pickup);
    CHECK (meta.exit == ExitCharacter::HandOff);
    CHECK (meta.isRoleFillable);
}

TEST_CASE ("grammatical links relate a phrase to others", "[phrase]")
{
    // White paper Part 8.5: call/response, statement/variation, etc. are
    // first-class metadata — information the musician's mind already carries.
    PhraseMetadata meta;
    meta.role = "response";
    meta.grammaticalLinks.push_back ({ GrammaticalLink::Kind::CallAndResponse,
                                       ConstituentId (1) });
    meta.grammaticalLinks.push_back ({ GrammaticalLink::Kind::StatementVariation,
                                       ConstituentId (2) });

    REQUIRE (meta.grammaticalLinks.size() == 2);
    CHECK (meta.grammaticalLinks[0].kind == GrammaticalLink::Kind::CallAndResponse);
    CHECK (meta.grammaticalLinks[0].target == ConstituentId (1));
    CHECK (meta.grammaticalLinks[1].kind == GrammaticalLink::Kind::StatementVariation);
    CHECK (meta.grammaticalLinks[1].target == ConstituentId (2));
}
