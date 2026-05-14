#pragma once

#include "sirius/ConstituentId.h"
#include "sirius/Position.h"
#include "sirius/Rational.h"

#include <variant>

namespace sirius
{

/// The five orthogonal dimensions that describe how a loop plays back (white
/// paper Part X). Defaults handle the common cases; combinations of non-default
/// values produce the interesting music.
///
/// Each dimension is a sum type — modelled as a std::variant of small structs
/// so that illegal combinations are unrepresentable. At M3 this is a faithful
/// *data model*; the playback *behaviour* (what "probabilistic" or "decaying"
/// actually does) belongs to the render pipeline.

// --- Trigger: what causes the idea to play? ---
namespace trigger
{
    /// Starts at capture and plays continuously.
    struct FreeRunning {};
    /// Dormant until triggered — by a footswitch, pad, MIDI note, etc.
    struct OnDemand {};
    /// Conditional: triggered every `bars` bars. Throws if `bars` < 1.
    struct EveryNBars { explicit EveryNBars (int bars); int bars; };
    /// Conditional: triggered after another Constituent ends.
    struct AfterConstituent { ConstituentId reference; };
    /// Sequenced: triggered at an explicit position in a timeline.
    struct AtPosition { Position position; };
    /// Probabilistic: a chance, in [0, 1], of triggering per cycle. Throws if
    /// the chance is outside that range.
    struct Probabilistic { explicit Probabilistic (Rational chancePerCycle); Rational chancePerCycle; };
}
using Trigger = std::variant<trigger::FreeRunning, trigger::OnDemand,
                             trigger::EveryNBars, trigger::AfterConstituent,
                             trigger::AtPosition, trigger::Probabilistic>;

// --- Cardinality: how many times does it repeat? ---
namespace cardinality
{
    /// A one-shot — a hit, a fill, a moment.
    struct Once {};
    /// A fixed count. Throws if `count` < 1.
    struct NTimes { explicit NTimes (int count); int count; };
    /// Until explicitly silenced.
    struct UntilSilenced {};
    /// Until another Constituent starts.
    struct UntilConstituentStarts { ConstituentId reference; };
    /// Until the next downbeat.
    struct UntilNextDownbeat {};
    /// Repeats indefinitely.
    struct Forever {};
}
using Cardinality = std::variant<cardinality::Once, cardinality::NTimes,
                                 cardinality::UntilSilenced,
                                 cardinality::UntilConstituentStarts,
                                 cardinality::UntilNextDownbeat,
                                 cardinality::Forever>;

// --- Phase: when does each repetition start? ---
namespace phase
{
    /// At trigger time, with no quantization.
    struct Free {};
    /// Snaps to the next grid `division`, in whole notes (e.g. 1/4 for a beat).
    /// Throws if the division is not positive.
    struct QuantizedToGrid { explicit QuantizedToGrid (Rational division); Rational division; };
    /// Starts at a fixed `offset` (whole notes) from another Constituent.
    struct SynchronizedTo { ConstituentId reference; Rational offset; };
    /// Maintains a fixed musical `offset` (whole notes) from a reference.
    struct PhaseLocked { ConstituentId reference; Rational offset; };
}
using Phase = std::variant<phase::Free, phase::QuantizedToGrid,
                           phase::SynchronizedTo, phase::PhaseLocked>;

// --- Mutation: does the idea change as it repeats? ---
/// At M3 the kind is the whole story; the parameters of automatic variation
/// (pitch/time/filter ranges, evolution rules) belong to the mutation engine,
/// a later milestone. The art of mutation is its invisibility (white paper
/// Part 10.3).
enum class Mutation
{
    Identical,           ///< the same data, every time
    VariedAutomatically, ///< pitch/time/filter varied per cycle
    Layered,             ///< each repetition adds another overdub
    Decaying,            ///< each repetition softer/darker/shorter
    EvolvingByRule       ///< granular drift, rhythmic permutation, Markov choice
};

// --- Termination: how does the repetition end? ---
namespace termination
{
    /// Instantly silent on stop.
    struct HardCut {};
    /// Finishes what is playing, then stops.
    struct CompleteCurrentCycle {};
    /// Fades over `bars` bars. Throws if `bars` is not positive.
    struct FadeOverBars { explicit FadeOverBars (Rational bars); Rational bars; };
    /// Plays until the content's own arc resolves.
    struct ContinueUntilNaturalEnd {};
    /// Ending triggers another Constituent's beginning.
    struct HandOff { ConstituentId next; };
}
using Termination = std::variant<termination::HardCut, termination::CompleteCurrentCycle,
                                 termination::FadeOverBars,
                                 termination::ContinueUntilNaturalEnd,
                                 termination::HandOff>;

/// The five dimensions together. The default-constructed value is the system's
/// best guess for a loop (white paper Part 10.2): free-running, forever,
/// unquantized, identical, completing the current cycle on stop.
struct RepetitionRules
{
    Trigger     trigger     { trigger::FreeRunning {} };
    Cardinality cardinality { cardinality::Forever {} };
    Phase       phase       { phase::Free {} };
    Mutation    mutation    { Mutation::Identical };
    Termination termination { termination::CompleteCurrentCycle {} };

    /// The default loop rules — identical to a default-constructed RepetitionRules,
    /// named for intent at call sites.
    static RepetitionRules defaultLoop();

    /// The default rules for a non-looped tape slice — "a loop with cardinality
    /// = Once" (white paper Part 7.5): it plays once, in its moment, and is
    /// allowed to finish.
    static RepetitionRules defaultOneShot();
};

} // namespace sirius
