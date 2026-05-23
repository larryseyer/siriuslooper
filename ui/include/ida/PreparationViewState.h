#pragma once

#include "ida/Constituent.h"
#include "ida/Rational.h"

#include <string>
#include <vector>

namespace sirius
{

/// One row in the Preparation view's tree readout — the dense, "fine and
/// precise" inverse of the Performance view (white paper Part 14.6). One row
/// per Constituent, indented to its depth in the tree, carrying every
/// glanceable identifier the performer needs to navigate the structure when
/// they are *allowed* to look at the screen.
struct PreparationRow
{
    int           indentLevel { 0 };
    ConstituentId id           { 0 };
    std::string   name;            ///< empty when the Constituent has none
    std::string   kind;            ///< "loop" | "phrase" | "group"
    Rational      durationWholeNotes; ///< the Constituent's own span length
    bool          hasEffectChain { false };
    int           effectCount    { 0 };
    bool          hasLocalMeter  { false };
    bool          hasLocalTempoMap { false };
    bool          isRoleFillable { false }; ///< true for role-fillable phrases (8.4)
};

/// The Preparation view's full state: every row of the Constituent tree, in
/// depth-first order. The view is purely structural — it does not change with
/// the playhead, since preparation is the cognitive mode the performer enters
/// *between* gestures, not during them.
struct PreparationViewState
{
    std::vector<PreparationRow> rows;
};

/// Computes the Preparation-view state from a Constituent tree.
PreparationViewState selectPreparationView (const Constituent& root);

} // namespace sirius
