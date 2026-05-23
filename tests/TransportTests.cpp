// Tests for the ensemble transport-message data model (white paper Part
// 12.2). The model is small — three message kinds in a variant — so these
// tests just pin down that each kind constructs cleanly, carries its
// load-bearing fields, and is pattern-matchable. The honest network surface
// (real sockets, frame format, reliability) is the operator-deferred half of
// M8 and tracked in todo.md.
#include "sirius/Transport.h"

#include <catch2/catch_test_macros.hpp>

#include <variant>

using ida::DisciplineTier;
using ida::EnsembleMessage;
using ida::LmcTimeAnnouncement;
using ida::MarkerEvent;
using ida::Rational;
using ida::TransportStateChange;

TEST_CASE ("an LMC time announcement carries its node, tier, time, and width",
           "[transport]")
{
    EnsembleMessage msg = LmcTimeAnnouncement { 1, DisciplineTier::Gps,
                                                Rational (1234, 1000),
                                                Rational (1, 1000) };
    REQUIRE (std::holds_alternative<LmcTimeAnnouncement> (msg));
    const auto& a = std::get<LmcTimeAnnouncement> (msg);
    CHECK (a.sourceNodeId == 1);
    CHECK (a.sourceTier == DisciplineTier::Gps);
    CHECK (a.lmcTime == Rational (1234, 1000));
    CHECK (a.intervalWidth == Rational (1, 1000));
}

TEST_CASE ("a marker event correlates a Constituent identity with an LMC moment",
           "[transport]")
{
    // The marker carries a ConstituentId so the receiver can correlate it
    // with its own Constituent graph after a merge (Part 12.6 → 12.2 link).
    EnsembleMessage msg = MarkerEvent { 2,
                                        ida::ConstituentId (42),
                                        Rational (5) };
    REQUIRE (std::holds_alternative<MarkerEvent> (msg));
    const auto& m = std::get<MarkerEvent> (msg);
    CHECK (m.sourceNodeId == 2);
    CHECK (m.markerId == ida::ConstituentId (42));
    CHECK (m.lmcTime == Rational (5));
}

TEST_CASE ("a transport state change names the new state and when it took effect",
           "[transport]")
{
    EnsembleMessage msg = TransportStateChange { 3,
                                                 TransportStateChange::State::Recording,
                                                 Rational (10) };
    REQUIRE (std::holds_alternative<TransportStateChange> (msg));
    const auto& t = std::get<TransportStateChange> (msg);
    CHECK (t.sourceNodeId == 3);
    CHECK (t.state == TransportStateChange::State::Recording);
    CHECK (t.lmcTime == Rational (10));
}

TEST_CASE ("ensemble messages are pattern-matched via std::visit", "[transport]")
{
    // The variant is the only API surface other components see; a visitor
    // covering every alternative is the idiomatic dispatch.
    auto kindOf = [] (const EnsembleMessage& m) -> const char*
    {
        return std::visit ([] (const auto& payload) -> const char*
        {
            using T = std::decay_t<decltype (payload)>;
            if constexpr (std::is_same_v<T, LmcTimeAnnouncement>)  return "lmc";
            else if constexpr (std::is_same_v<T, MarkerEvent>)     return "marker";
            else if constexpr (std::is_same_v<T, TransportStateChange>) return "transport";
            else                                                   return "?";
        }, m);
    };

    CHECK (std::string (kindOf (LmcTimeAnnouncement { 1, DisciplineTier::Ntp,
                                                      Rational (0), Rational (1) }))
           == "lmc");
    CHECK (std::string (kindOf (MarkerEvent { 1, ida::ConstituentId (0),
                                              Rational (0) }))
           == "marker");
    CHECK (std::string (kindOf (TransportStateChange { 1,
                                                       TransportStateChange::State::Stopped,
                                                       Rational (0) }))
           == "transport");
}
