#include "ida/PreparationViewState.h"

namespace sirius
{

namespace
{
    const char* kindOf (const Constituent& c)
    {
        if (c.isLoop())   return "loop";
        if (c.isPhrase()) return "phrase";
        return "group";
    }

    void walk (const Constituent& c, int indent, PreparationViewState& out)
    {
        PreparationRow row;
        row.indentLevel        = indent;
        row.id                 = c.id();
        row.name               = c.name();
        row.kind               = kindOf (c);
        row.durationWholeNotes = c.duration();
        row.hasEffectChain     = c.hasEffectChain();
        row.effectCount        = c.hasEffectChain()
                               ? static_cast<int> (c.effectChain()->size()) : 0;
        row.hasLocalMeter      = c.localMeter().has_value();
        row.hasLocalTempoMap   = c.localTempoMap().has_value();
        row.isRoleFillable     = c.isPhrase() && c.phraseMetadata()->isRoleFillable;
        out.rows.push_back (std::move (row));

        for (const auto& child : c.children())
            walk (*child, indent + 1, out);
    }
}

PreparationViewState selectPreparationView (const Constituent& root)
{
    PreparationViewState state;
    walk (root, 0, state);
    return state;
}

} // namespace sirius
