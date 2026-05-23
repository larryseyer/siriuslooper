#include "ida/RepetitionRules.h"

#include <stdexcept>

namespace ida
{

namespace trigger
{
    EveryNBars::EveryNBars (int barsIn) : bars (barsIn)
    {
        if (bars < 1)
            throw std::invalid_argument ("ida::trigger::EveryNBars: bars must be >= 1");
    }

    Probabilistic::Probabilistic (Rational chanceIn) : chancePerCycle (chanceIn)
    {
        if (chancePerCycle < Rational (0) || chancePerCycle > Rational (1))
            throw std::invalid_argument (
                "ida::trigger::Probabilistic: chance per cycle must be in [0, 1]");
    }
}

namespace cardinality
{
    NTimes::NTimes (int countIn) : count (countIn)
    {
        if (count < 1)
            throw std::invalid_argument ("ida::cardinality::NTimes: count must be >= 1");
    }
}

namespace phase
{
    QuantizedToGrid::QuantizedToGrid (Rational divisionIn) : division (divisionIn)
    {
        if (division.isZero() || division.isNegative())
            throw std::invalid_argument (
                "ida::phase::QuantizedToGrid: division must be positive");
    }
}

namespace termination
{
    FadeOverBars::FadeOverBars (Rational barsIn) : bars (barsIn)
    {
        if (bars.isZero() || bars.isNegative())
            throw std::invalid_argument (
                "ida::termination::FadeOverBars: bars must be positive");
    }
}

RepetitionRules RepetitionRules::defaultLoop()
{
    // The default member initializers already encode the loop defaults.
    return RepetitionRules {};
}

RepetitionRules RepetitionRules::defaultOneShot()
{
    RepetitionRules rules;
    rules.trigger     = trigger::FreeRunning {};
    rules.cardinality = cardinality::Once {};
    rules.phase       = phase::Free {};
    rules.mutation    = Mutation::Identical;
    rules.termination = termination::ContinueUntilNaturalEnd {};
    return rules;
}

} // namespace ida
