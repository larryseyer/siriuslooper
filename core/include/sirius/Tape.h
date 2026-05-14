#pragma once

#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/TapeId.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace sirius
{

/// One event on a tape, in the uniform format every tape shares (white paper
/// Part 6.2). It carries two timestamps: the LMC timestamp is the truth about
/// when the event arrived at the membrane; the conceptual timestamp is the
/// truth about what musical moment that arrival represents.
///
/// `Payload` is the kind of data the tape carries — audio samples, video
/// frames, MIDI events, control values, parameter automation. All tapes share
/// this structure; only the payload differs.
template <typename Payload>
struct TapeEvent
{
    Position conceptualTimestamp; ///< when, in the source's conceptual time
    Rational lmcTimestamp;        ///< when, in LMC absolute time (seconds)
    TapeId   tapeId;              ///< which input this event belongs to
    Payload  payload;            ///< the data itself
};

/// An append-only, immutable stream of timestamped events from a single input
/// source — the source of truth (white paper Part VI). A tape is written once,
/// at the moment of capture, and never edited: events can be appended at the
/// end, but existing events are never modified or removed. All editing in the
/// system happens elsewhere, in Constituents.
template <typename Payload>
class Tape
{
public:
    using Event = TapeEvent<Payload>;

    explicit Tape (TapeId id) : id_ (id) {}

    TapeId      id()    const noexcept { return id_; }
    bool        empty() const noexcept { return events_.empty(); }
    std::size_t size()  const noexcept { return events_.size(); }

    const std::vector<Event>& events() const noexcept { return events_; }

    /// Appends an event. A tape records forward in time, so the event's LMC
    /// timestamp must not precede the last event's. The event's tapeId must
    /// match this tape. Throws std::invalid_argument otherwise. This is the
    /// only mutating operation — events are never modified or removed.
    void append (Event event)
    {
        if (event.tapeId != id_)
            throw std::invalid_argument ("sirius::Tape: event belongs to a different tape");

        if (! events_.empty() && event.lmcTimestamp < events_.back().lmcTimestamp)
            throw std::invalid_argument (
                "sirius::Tape: events must be appended in non-decreasing LMC time");

        events_.push_back (std::move (event));
    }

    /// Events whose LMC timestamp falls in the half-open range [start, end).
    std::vector<Event> eventsInLmcRange (Rational start, Rational end) const
    {
        std::vector<Event> result;
        for (const Event& e : events_)
            if (e.lmcTimestamp >= start && e.lmcTimestamp < end)
                result.push_back (e);
        return result;
    }

    /// Events whose conceptual timestamp falls in the half-open range
    /// [start, end).
    std::vector<Event> eventsInConceptualRange (Position start, Position end) const
    {
        std::vector<Event> result;
        for (const Event& e : events_)
            if (e.conceptualTimestamp >= start && e.conceptualTimestamp < end)
                result.push_back (e);
        return result;
    }

private:
    TapeId id_;
    std::vector<Event> events_;
};

} // namespace sirius
