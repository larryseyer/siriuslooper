// DemoSession shape — the canonical reference operators look at first should
// honor the convention promotion enforces: Loops are leaves with TapeReference;
// Phrases are containers with PhraseMetadata. A single Constituent must never
// be both at once ("hybrid"). This test locks the shape so a future edit that
// reintroduces a hybrid surfaces immediately instead of in operator confusion.

#include "DemoSession.h"

#include "sirius/Constituent.h"

#include <catch2/catch_test_macros.hpp>

using namespace sirius;

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

    for (const auto& topLevel : demo.root->children())
    {
        REQUIRE (topLevel != nullptr);
        // Every top-level child is a role-bearing Phrase: intro, verse, outro.
        REQUIRE (topLevel->isPhrase());
        // No top-level child is a hybrid Phrase+Loop Constituent. Loops are
        // leaves; Phrases are containers. Hybrids belong to neither category.
        REQUIRE_FALSE (isHybrid (*topLevel));
        // Each top-level Phrase has at least one Loop descendant so the Pills
        // aggregator can show it on the timeline.
        REQUIRE (hasLoopChild (*topLevel));
    }
}
