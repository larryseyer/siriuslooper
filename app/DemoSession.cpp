#include "DemoSession.h"

#include "sirius/Arrangement.h"
#include "sirius/InputDescriptor.h"
#include "sirius/InputKind.h"
#include "sirius/Phrase.h"
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

    PhraseMetadata phraseMeta (const char* role, EntranceCharacter ent,
                               ExitCharacter ex)
    {
        PhraseMetadata m;
        m.role     = role;
        m.entrance = ent;
        m.exit     = ex;
        return m;
    }
}

DemoSession buildDemoSession()
{
    // Every top-level child of the session is a Phrase *container* — never a
    // Phrase-and-Loop hybrid. Even single-tape sections (intro, outro) wrap
    // their tape in a child Loop so the rule "Loops are leaves; Phrases are
    // containers" holds uniformly across the tree.
    const Constituent introPhraseShell =
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withName ("intro")
            .withPhraseMetadata (phraseMeta ("intro",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::HandOff));
    const auto intro = std::make_shared<const Constituent> (
        arrangement::layer (introPhraseShell,
            { makeLoop (11, "intro", Rational (3), 100) }));

    // The middle phrase is a layer of two simultaneous loops — a rhythm bed and
    // a lead line — so the demo exercises both arrangement primitives and the
    // multi-tape Pill case (primary tape = rhythm; member = {rhythm, lead}).
    const Constituent versePhraseShell =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (phraseMeta ("verse",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::HandOff));
    const auto verse = std::make_shared<const Constituent> (
        arrangement::layer (versePhraseShell,
            { makeLoop (21, "verse: rhythm", Rational (6), 200),
              makeLoop (22, "verse: lead",   Rational (3), 300) }));

    const Constituent outroPhraseShell =
        Constituent (ConstituentId (30), Position(), Position (Rational (3)))
            .withName ("outro")
            .withPhraseMetadata (phraseMeta ("outro",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::Resolution));
    const auto outro = std::make_shared<const Constituent> (
        arrangement::layer (outroPhraseShell,
            { makeLoop (31, "outro", Rational (3), 400) }));

    // The three phrases run end-to-end under the session: intro [0,3),
    // verse [3,9), outro [9,12) — twelve whole notes total.
    const Constituent sessionShell =
        Constituent (ConstituentId (1), Position(), Position (Rational (12)))
            .withName ("demo session");
    const auto session = std::make_shared<const Constituent> (
        arrangement::sequence (sessionShell, { intro, verse, outro }));

    // 120 quarter-note BPM => one whole note is 2 LMC seconds, so the
    // twelve-whole-note session spans 24 LMC seconds.
    TempoMap sessionToLmc = TempoMap::fromBpm (Rational (120));
    const Rational lengthSeconds = sessionToLmc.apply (Rational (12));

    // Input descriptors for the demo's four tapes. These map TapeId → kind +
    // user-visible name and drive the TimelineView's strip rows.
    std::vector<InputDescriptor> inputs;
    inputs.push_back ({ TapeId (100), InputKind::Audio, "Intro pad",   0 });
    inputs.push_back ({ TapeId (200), InputKind::Audio, "Verse rhythm",1 });
    inputs.push_back ({ TapeId (300), InputKind::Audio, "Verse lead",  2 });
    inputs.push_back ({ TapeId (400), InputKind::Audio, "Outro pad",   3 });

    return DemoSession { session, std::move (sessionToLmc), lengthSeconds,
                         std::move (inputs) };
}

} // namespace sirius
