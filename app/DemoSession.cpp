#include "DemoSession.h"

#include "ida/Arrangement.h"
#include "ida/InputDescriptor.h"
#include "ida/InputKind.h"
#include "ida/Phrase.h"
#include "ida/Position.h"
#include "ida/TapeId.h"
#include "ida/TapeReference.h"

namespace ida
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
    // Intro [0,3) — bare Phrase containing one Loop. Convention unchanged:
    // every top-level child is a Phrase container, never a hybrid.
    const Constituent introPhraseShell =
        Constituent (ConstituentId (10), Position(), Position (Rational (3)))
            .withName ("intro")
            .withPhraseMetadata (phraseMeta ("intro",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::HandOff));
    const auto intro = std::make_shared<const Constituent> (
        arrangement::layer (introPhraseShell,
            { makeLoop (11, "intro", Rational (3), 1) }));

    // Verse — ONE shared Phrase that the song places three times. The shared
    // Phrase is a layer of two simultaneous loops (rhythm + lead) and lives
    // at id 20. sequenceShared then mints three wrapper Constituents (ids
    // 51, 52, 53) at offsets 3, 9, 15, all pointing at this same ChildPtr.
    const Constituent versePhraseShell =
        Constituent (ConstituentId (20), Position(), Position (Rational (6)))
            .withName ("verse")
            .withPhraseMetadata (phraseMeta ("verse",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::HandOff));
    const auto verse = std::make_shared<const Constituent> (
        arrangement::layer (versePhraseShell,
            { makeLoop (21, "verse: rhythm", Rational (6), 2),
              makeLoop (22, "verse: lead",   Rational (3), 3) }));

    // Outro [21,24).
    const Constituent outroPhraseShell =
        Constituent (ConstituentId (30), Position(), Position (Rational (3)))
            .withName ("outro")
            .withPhraseMetadata (phraseMeta ("outro",
                                             EntranceCharacter::Downbeat,
                                             ExitCharacter::Resolution));
    const auto outro = std::make_shared<const Constituent> (
        arrangement::layer (outroPhraseShell,
            { makeLoop (31, "outro", Rational (3), 4) }));

    // Build the song: intro at [0,3), three verse wrappers at [3,9), [9,15),
    // [15,21), outro at [21,24). Twenty-four whole notes total.
    std::int64_t nextWrapperId = 51;
    auto allocateWrapper = [&nextWrapperId] { return ConstituentId (nextWrapperId++); };

    const Constituent sessionShell =
        Constituent (ConstituentId (1), Position(), Position (Rational (24)))
            .withName ("demo session");

    const Constituent withIntro =
        arrangement::sequence (sessionShell, { intro });
    const Constituent withVerses =
        arrangement::sequenceShared (withIntro, verse,
            { Position (Rational (3)), Position (Rational (9)), Position (Rational (15)) },
            allocateWrapper);
    const auto session = std::make_shared<const Constituent> (
        arrangement::sequence (withVerses, { outro }));

    // 120 quarter-note BPM => one whole note is 2 LMC seconds, so the
    // twenty-four-whole-note session spans 48 LMC seconds.
    TempoMap sessionToLmc = TempoMap::fromBpm (Rational (120));
    const Rational lengthSeconds = sessionToLmc.apply (Rational (24));

    std::vector<InputDescriptor> inputs;
    // Tape ids match the leaf-loop tape references above (1..4) so that live
    // capture — which commits each input strip to a real pool tape — feeds the
    // phrase that reads the same tape. Id 1 is first so it becomes the pool's
    // permanent primary (the default strip destination). See MainComponent's
    // tape-pool seeding from these descriptors.
    inputs.push_back ({ TapeId (1), InputKind::Audio, "Intro pad",   0 });
    inputs.push_back ({ TapeId (2), InputKind::Audio, "Verse rhythm",1 });
    inputs.push_back ({ TapeId (3), InputKind::Audio, "Verse lead",  2 });
    inputs.push_back ({ TapeId (4), InputKind::Audio, "Outro pad",   3 });

    return DemoSession { session, std::move (sessionToLmc), lengthSeconds,
                         std::move (inputs) };
}

} // namespace ida
