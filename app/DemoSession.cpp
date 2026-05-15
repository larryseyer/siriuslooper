#include "DemoSession.h"

#include "sirius/Arrangement.h"
#include "sirius/Position.h"
#include "sirius/TapeId.h"
#include "sirius/TapeReference.h"

namespace sirius
{

namespace
{
    /// A leaf loop Constituent: a named, [0,length)-spanning Constituent that
    /// references the slice [0, 2*length) of `tape`. The tape slice is
    /// deliberately longer than one placement span so the render pipeline shows
    /// the read head advancing rather than immediately recycling.
    std::shared_ptr<const Constituent> makeLoop (std::int64_t id,
                                                 const char* name,
                                                 Rational length,
                                                 std::int64_t tape)
    {
        const Constituent loop =
            Constituent (ConstituentId (id), Position(), Position (length))
                .withName (name)
                .withTapeReference (
                    TapeReference (TapeId (tape), Rational (0), length * Rational (2)));
        return std::make_shared<const Constituent> (loop);
    }
}

DemoSession buildDemoSession()
{
    // The middle phrase is a layer of two simultaneous loops — a rhythm bed and
    // a lead line — so the demo exercises both arrangement primitives.
    const Constituent versePhrase =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withName ("verse");
    const auto verse = std::make_shared<const Constituent> (
        arrangement::layer (versePhrase, { makeLoop (21, "verse: rhythm", Rational (6), 200),
                                           makeLoop (22, "verse: lead",   Rational (3), 300) }));

    // The three phrases run end-to-end under the session: intro [0,3),
    // verse [3,9), outro [9,12) — twelve whole notes total.
    const Constituent sessionShell =
        Constituent (ConstituentId (1), Position(), Position (Rational (12)))
            .withName ("demo session");
    const auto session = std::make_shared<const Constituent> (
        arrangement::sequence (sessionShell, { makeLoop (10, "intro", Rational (3), 100),
                                               verse,
                                               makeLoop (30, "outro", Rational (3), 400) }));

    // 120 quarter-note BPM => one whole note is 2 LMC seconds, so the
    // twelve-whole-note session spans 24 LMC seconds.
    TempoMap sessionToLmc = TempoMap::fromBpm (Rational (120));
    const Rational lengthSeconds = sessionToLmc.apply (Rational (12));

    return DemoSession { session, std::move (sessionToLmc), lengthSeconds };
}

} // namespace sirius
