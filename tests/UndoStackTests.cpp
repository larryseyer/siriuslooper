// Tests for the multi-level undo/redo stack (white paper Part 14.7). These
// pin down the architectural payoff of the immutable-Constituent data model:
// undo is just a stack of root pointers — instantaneous, multi-level,
// reversible, and visible — without diffs, replay, or any special-case state.
#include "sirius/UndoStack.h"

#include "sirius/Constituent.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>

using sirius::Constituent;
using sirius::ConstituentId;
using sirius::Position;
using sirius::Rational;
using sirius::UndoStack;

namespace
{
    UndoStack::RootPtr makeRoot (std::int64_t id, const char* name = "")
    {
        return std::make_shared<const Constituent> (
            Constituent (ConstituentId (id), Position(), Position (Rational (4)))
                .withName (name));
    }
}

TEST_CASE ("a fresh stack holds the initial root and has nothing to undo or redo",
           "[undo]")
{
    const auto initial = makeRoot (1, "root");
    UndoStack stack (initial);
    CHECK (stack.current().get() == initial.get());
    CHECK_FALSE (stack.canUndo());
    CHECK_FALSE (stack.canRedo());
    CHECK (stack.depth() == 1);
}

TEST_CASE ("UndoStack rejects null inputs and zero depth", "[undo]")
{
    CHECK_THROWS_AS (UndoStack (nullptr), std::invalid_argument);
    CHECK_THROWS_AS (UndoStack (makeRoot (1), 0), std::invalid_argument);

    UndoStack stack (makeRoot (1));
    CHECK_THROWS_AS (stack.push (nullptr), std::invalid_argument);
}

TEST_CASE ("push advances current and exposes a descriptive label", "[undo]")
{
    UndoStack stack (makeRoot (1, "initial"));
    stack.push (makeRoot (2, "renamed"), "rename phrase");

    CHECK (stack.canUndo());
    CHECK_FALSE (stack.canRedo());
    CHECK (stack.nextUndoLabel() == "rename phrase");
    CHECK (stack.current()->name() == "renamed");
}

TEST_CASE ("undo and redo are inverse operations", "[undo]")
{
    const auto a = makeRoot (1, "A");
    const auto b = makeRoot (2, "B");
    const auto c = makeRoot (3, "C");

    UndoStack stack (a);
    stack.push (b, "AtoB");
    stack.push (c, "BtoC");

    // Reach the start; then return — pointer-equality across the trip is the
    // strongest demonstration that undo is non-destructive.
    CHECK (stack.undo().get() == b.get());
    CHECK (stack.undo().get() == a.get());
    CHECK_FALSE (stack.canUndo());
    CHECK (stack.canRedo());
    CHECK (stack.redo().get() == b.get());
    CHECK (stack.redo().get() == c.get());
    CHECK_FALSE (stack.canRedo());
}

TEST_CASE ("undo and redo on an empty branch are silent no-ops", "[undo]")
{
    // A performer hits undo reflexively when they think they want to. Throwing
    // there would punish the reflex — match what the gesture actually does.
    UndoStack stack (makeRoot (1, "only"));
    CHECK (stack.undo().get() == stack.current().get());
    CHECK (stack.redo().get() == stack.current().get());
}

TEST_CASE ("a fresh edit invalidates the redo branch", "[undo]")
{
    // White paper 14.7: redo is invalidated only when the user makes a fresh
    // edit after undoing — once the alternate future is gone, it stays gone.
    UndoStack stack (makeRoot (1, "A"));
    stack.push (makeRoot (2, "B"));
    stack.push (makeRoot (3, "C"));
    stack.undo();
    REQUIRE (stack.canRedo());

    stack.push (makeRoot (4, "D"));
    CHECK_FALSE (stack.canRedo());
    CHECK (stack.current()->name() == "D");
}

TEST_CASE ("the stack caps depth at the configured maximum, dropping the oldest",
           "[undo]")
{
    // White paper 14.7: "multi-level, with depth limited only by storage."
    // The cap exists so a long session does not grow the history unbounded.
    UndoStack stack (makeRoot (0, "0"), /*maxDepth*/ 3);
    stack.push (makeRoot (1, "1"));
    stack.push (makeRoot (2, "2"));
    stack.push (makeRoot (3, "3"));

    CHECK (stack.depth() == 3);
    CHECK (stack.current()->name() == "3");

    // Undoing all the way reaches "1", not "0" — the oldest entry was dropped.
    while (stack.canUndo()) stack.undo();
    CHECK (stack.current()->name() == "1");
}
