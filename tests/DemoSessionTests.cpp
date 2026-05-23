// DemoSession shape — the canonical reference operators look at first should
// honor the convention promotion enforces: Loops are leaves with TapeReference;
// Phrases are containers with PhraseMetadata. A single Constituent must never
// be both at once ("hybrid"). This test locks the shape so a future edit that
// reintroduces a hybrid surfaces immediately instead of in operator confusion.

#include "DemoSession.h"

#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/Rational.h"

#include <catch2/catch_test_macros.hpp>

using namespace ida;

namespace
{
    bool isHybrid (const Constituent& c)
    {
        return c.isPhrase() && c.tapeReference().has_value();
    }

    bool hasLoopChild (const Constituent& c)
    {
        for (const auto& child : c.children())
            if (child->isLoop())
                return true;
        return false;
    }
}

TEST_CASE ("DemoSession top-level children are all Phrase shells, never hybrids",
           "[demoSession][shape]")
{
    const auto demo = buildDemoSession();
    REQUIRE (demo.root != nullptr);
    REQUIRE_FALSE (demo.root->children().empty());

    // Five top-level children: intro, three verse wrappers, outro.
    REQUIRE (demo.root->children().size() == 5);

    // Intro [0,3) — bare Phrase, has a Loop descendant.
    {
        const auto& intro = *demo.root->children()[0];
        REQUIRE (intro.isPhrase());
        REQUIRE_FALSE (isHybrid (intro));
        REQUIRE (hasLoopChild (intro));
        CHECK (intro.conceptualIn()  == ida::Position (ida::Rational (0)));
        CHECK (intro.conceptualOut() == ida::Position (ida::Rational (3)));
    }

    // Three verse wrappers at [3,9), [9,15), [15,21).
    for (std::size_t i = 1; i <= 3; ++i)
    {
        const auto& wrapper = *demo.root->children()[i];
        REQUIRE (ida::isPlacementWrapper (wrapper));
        REQUIRE_FALSE (isHybrid (wrapper));
        // Wrapper itself has no direct Loop child (the shared verse does).
        CHECK (wrapper.conceptualIn()  ==
               ida::Position (ida::Rational (3 + static_cast<int> (i - 1) * 6)));
        CHECK (wrapper.conceptualOut() ==
               ida::Position (ida::Rational (3 + static_cast<int> (i) * 6)));
    }

    // Outro [21,24) — bare Phrase, has a Loop descendant.
    {
        const auto& outro = *demo.root->children()[4];
        REQUIRE (outro.isPhrase());
        REQUIRE_FALSE (isHybrid (outro));
        REQUIRE (hasLoopChild (outro));
        CHECK (outro.conceptualIn()  == ida::Position (ida::Rational (21)));
        CHECK (outro.conceptualOut() == ida::Position (ida::Rational (24)));
    }

    // Total span: 24 whole notes.
    CHECK (demo.root->conceptualOut() == ida::Position (ida::Rational (24)));
}

TEST_CASE ("DemoSession's three verse wrappers share one Phrase ChildPtr",
           "[demoSession][shared]")
{
    const auto demo = buildDemoSession();
    REQUIRE (demo.root->children().size() == 5);

    const auto& wrapperA = *demo.root->children()[1];
    const auto& wrapperB = *demo.root->children()[2];
    const auto& wrapperC = *demo.root->children()[3];
    REQUIRE (wrapperA.children().size() >= 1);
    REQUIRE (wrapperB.children().size() >= 1);
    REQUIRE (wrapperC.children().size() >= 1);

    // The canary: real sharing, not duplicate-id-by-mistake.
    const auto* a = wrapperA.children()[0].get();
    const auto* b = wrapperB.children()[0].get();
    const auto* c = wrapperC.children()[0].get();
    CHECK (a == b);
    CHECK (b == c);
    CHECK (a->id().value() == 20);  // canonical shared verse id
    CHECK (a->name() == "verse");
}
