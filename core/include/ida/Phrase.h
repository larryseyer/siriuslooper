#pragma once

#include "ida/ConstituentId.h"

#include <string>
#include <vector>

namespace ida
{

/// How a phrase begins (white paper Part 8.7). The entrance character is part
/// of the phrase's identity, not a separate concern — a phrase that begins with
/// a pickup carries its pickup as part of the phrase.
enum class EntranceCharacter
{
    Unspecified,
    Pickup,    ///< begins with a pickup / anacrusis
    Downbeat   ///< begins squarely on a downbeat
};

/// How a phrase ends (white paper Part 8.7). The exit character interacts with
/// arrangement: a phrase whose exit is HandOff implies the existence of a next
/// phrase to receive the hand-off.
enum class ExitCharacter
{
    Unspecified,
    Resolution, ///< ends by resolving — a self-contained close
    HandOff     ///< ends by handing off to the next phrase
};

/// A grammatical relationship between phrases (white paper Part 8.5). These do
/// not affect playback; they affect display, analysis, and composition. The
/// architecture surfaces information the musician's mind is already carrying.
struct GrammaticalLink
{
    enum class Kind
    {
        CallAndResponse,    ///< this phrase poses or answers a question
        StatementVariation, ///< this phrase is a variation of the target
        ThemeDevelopment,   ///< this phrase develops the target theme
        TensionRelease,     ///< this phrase builds toward or resolves the target
        Punctuation         ///< this phrase closes or opens a section around the target
    };

    Kind kind;
    ConstituentId target;
};

/// The metadata a phrase carries beyond what a loop carries (white paper Part
/// 8.3). A Constituent that carries PhraseMetadata is functioning as a phrase:
/// the unit of musical thought, with role, intent, and grammar.
///
/// This is plain data with no invariants — every field is freely settable —
/// so it is a struct, not a class.
struct PhraseMetadata
{
    std::string role;    ///< "verse", "chorus", "response", "fill", ... — open-ended
    std::string intent;  ///< free-text description of the phrase's musical purpose

    EntranceCharacter entrance { EntranceCharacter::Unspecified };
    ExitCharacter     exit     { ExitCharacter::Unspecified };

    std::vector<GrammaticalLink> grammaticalLinks;

    /// Whether this phrase can substitute for others of the same role — the
    /// basis of structured improvisation (white paper Part 8.4).
    bool isRoleFillable { false };
};

} // namespace ida
