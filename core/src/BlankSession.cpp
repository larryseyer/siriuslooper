#include "ida/BlankSession.h"

#include "ida/ConstituentId.h"
#include "ida/Position.h"

namespace ida
{

BlankSession buildBlankSession()
{
    // Root id 1 matches the demo session's root id so any code keyed to "the
    // session shell is id 1" keeps holding. A [0,0) span is a valid leaf
    // Constituent (conceptualOut does not precede conceptualIn) and reads as
    // "nothing placed yet". The shell is deliberately NOT a phrase and carries
    // no tape reference — it is a bare container the user fills via the Record
    // gesture (spec §8, a later slice).
    const Constituent shell =
        Constituent (ConstituentId (1), Position(), Position (Rational (0)))
            .withName ("Untitled");
    auto root = std::make_shared<const Constituent> (shell);

    // A real default tempo map so timeline/render math has something to apply;
    // length is zero because nothing is placed. fromBpm is pure.
    TempoMap sessionToLmc = TempoMap::fromBpm (Rational (120));

    return BlankSession { std::move (root), std::move (sessionToLmc),
                          Rational (0), {} };
}

} // namespace ida
