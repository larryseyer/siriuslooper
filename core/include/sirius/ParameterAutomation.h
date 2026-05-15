#pragma once

#include "sirius/Tape.h"

#include <stdexcept>

namespace sirius
{

/// A single sample of a hosted plugin's parameter value, in the format that
/// rides on a parameter tape (white paper Part 7.7: "Effect parameters are
/// themselves automatable. Parameter automation is data on a tape. Automation
/// curves are Constituents over parameter tapes. The recursion holds at every
/// level.").
///
/// The value is normalized to [0, 1] — the same convention JUCE's
/// `AudioProcessorParameter::getValue` uses — so the data model is independent
/// of any particular plugin's parameter range. The constructor enforces the
/// range, because a tape event that records an out-of-range value would be a
/// silent corruption of the automation curve.
struct ParameterEvent
{
    /// Constructs an event for `index`, with normalized value `value` in [0, 1].
    /// Throws std::invalid_argument otherwise.
    ParameterEvent (int index, double value)
        : parameterIndex (index), valueZeroToOne (value)
    {
        if (! (value >= 0.0 && value <= 1.0))
            throw std::invalid_argument (
                "sirius::ParameterEvent: value must be in [0, 1]");
    }

    int    parameterIndex;
    double valueZeroToOne;
};

/// A tape carrying parameter-automation events. This is just `Tape<...>` —
/// every tape in Sirius shares one structure (white paper Part 6.2); only the
/// payload differs. Spelling out the alias makes the recursion in the data
/// model explicit at call sites: a Constituent with a `tapeReference` into a
/// `ParameterTape` is an automation curve in exactly the same way a
/// Constituent with a `tapeReference` into an audio tape is a loop.
using ParameterTape = Tape<ParameterEvent>;

} // namespace sirius
