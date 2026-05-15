#pragma once

#include "sirius/ConstituentId.h"
#include "sirius/LmcElection.h"
#include "sirius/Rational.h"

#include <variant>

namespace sirius
{

/// The three kinds of message that cross the ensemble network (white paper
/// Part 12.2 — "what does cross the network"). Audio never appears here; the
/// network is a substrate for *coordination*, not signal. The data model is
/// kept JUCE-free and transport-free: real sockets, frame formats, and
/// reliability are the operator-deferred half of M8.

/// An LMC time announcement — the master broadcasts its current LMC time as
/// a confidence interval, the same shape the election consumed. Slaves
/// discipline against it.
struct LmcTimeAnnouncement
{
    int            sourceNodeId;
    DisciplineTier sourceTier;
    Rational       lmcTime;
    Rational       intervalWidth; ///< the +/- half-width around lmcTime
};

/// A musical marker — a phrase start, a section boundary, a hand-off. The
/// `markerId` is a ConstituentId so the receiver can correlate the event
/// with its own Constituent graph after merge.
struct MarkerEvent
{
    int           sourceNodeId;
    ConstituentId markerId;
    Rational      lmcTime;
};

/// Transport state — the ensemble's collective sense of "are we playing,
/// stopped, or recording" (Part 12.2). Each change carries the LMC time it
/// took effect so late-arriving nodes can backfill consistently.
struct TransportStateChange
{
    enum class State { Stopped, Playing, Recording };

    int       sourceNodeId;
    State     state;
    Rational  lmcTime;
};

/// Every coordination message, as one sum type. New kinds extend the variant
/// — the rest of the system pattern-matches on it.
using EnsembleMessage = std::variant<LmcTimeAnnouncement,
                                     MarkerEvent,
                                     TransportStateChange>;

} // namespace sirius
