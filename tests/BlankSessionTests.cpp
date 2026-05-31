// BlankSession is the boot state that replaces the demo song (spec §4.1, §11).
// It must be a *usable empty project*: an empty root Constituent (no phrases),
// no tapes, no channels. These tests lock that emptiness so a future edit that
// reseeds the boot with demo-like content surfaces immediately.

#include "ida/BlankSession.h"

#include "ida/Constituent.h"
#include "ida/Position.h"
#include "ida/Rational.h"
#include "ida/TapePool.h"

#include <catch2/catch_test_macros.hpp>

using namespace ida;

TEST_CASE ("buildBlankSession has an empty root Constituent (no phrases)", "[blankSession]")
{
    const auto blank = buildBlankSession();
    REQUIRE (blank.root != nullptr);
    CHECK (blank.root->children().empty());     // zero phrases at boot
    CHECK (blank.root->isLeaf());
    CHECK_FALSE (blank.root->isPhrase());        // the shell is a bare container
    CHECK_FALSE (blank.root->tapeReference().has_value());
}

TEST_CASE ("buildBlankSession has no inputs and no tapes referenced", "[blankSession]")
{
    const auto blank = buildBlankSession();
    CHECK (blank.inputs.empty());                // zero channels at boot (spec §4.1)
}

TEST_CASE ("buildBlankSession reports a zero session length", "[blankSession]")
{
    const auto blank = buildBlankSession();
    CHECK (blank.lengthLmcSeconds == Rational (0));
    CHECK (blank.root->conceptualOut() == Position (Rational (0)));
}

TEST_CASE ("eraseRequiresConfirmation is false for an empty project", "[blankSession][erase]")
{
    ida::TapePool empty;                          // Slice 1: default ctor is empty
    CHECK_FALSE (ida::eraseRequiresConfirmation (empty));
}

TEST_CASE ("eraseRequiresConfirmation is true once any tape exists", "[blankSession][erase]")
{
    ida::TapePool pool;
    pool.add ("Tape 1");
    CHECK (ida::eraseRequiresConfirmation (pool));   // §2.1: tapes present ⇒ must warn
    pool.add ("Tape 2");
    CHECK (ida::eraseRequiresConfirmation (pool));
}
