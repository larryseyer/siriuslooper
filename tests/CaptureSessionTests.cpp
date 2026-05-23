// Tests for ida::CaptureSession — the performer-facing capture state
// machine that sits above the always-running tape (white paper Part 7
// architecture vs Part 14.6 UX). Pins down each transition: arm/disarm
// gating, in-point setting and replacement, valid/invalid out-point
// handling, cancel semantics, and the tape-identity that markIn pins
// into AwaitingOut and that markOut stamps into the closed region.
#include "sirius/CaptureSession.h"

#include "sirius/Rational.h"
#include "sirius/TapeId.h"

#include <catch2/catch_test_macros.hpp>

using ida::CaptureSession;
using ida::CaptureState;
using ida::Rational;
using ida::TapeId;

TEST_CASE ("a fresh session is Disarmed with no pending in-point or tape",
           "[capture-session]")
{
    const CaptureSession s;
    CHECK (s.state() == CaptureState::Disarmed);
    CHECK_FALSE (s.isArmed());
    CHECK_FALSE (s.isCapturing());
    CHECK_FALSE (s.pendingIn().has_value());
    CHECK_FALSE (s.pendingTape().has_value());
}

TEST_CASE ("arm transitions Disarmed to Armed", "[capture-session]")
{
    CaptureSession s;
    s.arm();
    CHECK (s.state() == CaptureState::Armed);
    CHECK (s.isArmed());
    CHECK_FALSE (s.isCapturing());
}

TEST_CASE ("arm is idempotent on Armed and AwaitingOut",
           "[capture-session]")
{
    CaptureSession s;
    s.arm();
    s.arm();
    CHECK (s.state() == CaptureState::Armed);

    REQUIRE (s.markIn (Rational (1), TapeId (1)));
    REQUIRE (s.state() == CaptureState::AwaitingOut);

    s.arm();
    CHECK (s.state() == CaptureState::AwaitingOut);
    CHECK (s.pendingIn() == std::optional<Rational> (Rational (1)));
    CHECK (s.pendingTape() == std::optional<TapeId> (TapeId (1)));
}

TEST_CASE ("disarm from any state lands at Disarmed and clears pending in-point and tape",
           "[capture-session]")
{
    CaptureSession s;

    SECTION ("from Disarmed is a no-op")
    {
        s.disarm();
        CHECK (s.state() == CaptureState::Disarmed);
    }

    SECTION ("from Armed transitions to Disarmed")
    {
        s.arm();
        s.disarm();
        CHECK (s.state() == CaptureState::Disarmed);
        CHECK_FALSE (s.pendingIn().has_value());
        CHECK_FALSE (s.pendingTape().has_value());
    }

    SECTION ("from AwaitingOut transitions to Disarmed and discards in-point and tape")
    {
        s.arm();
        REQUIRE (s.markIn (Rational (3), TapeId (7)));
        REQUIRE (s.pendingIn().has_value());
        REQUIRE (s.pendingTape().has_value());

        s.disarm();
        CHECK (s.state() == CaptureState::Disarmed);
        CHECK_FALSE (s.pendingIn().has_value());
        CHECK_FALSE (s.pendingTape().has_value());
    }
}

TEST_CASE ("markIn is ignored while Disarmed", "[capture-session]")
{
    CaptureSession s;
    const bool accepted = s.markIn (Rational (1), TapeId (1));
    CHECK_FALSE (accepted);
    CHECK (s.state() == CaptureState::Disarmed);
    CHECK_FALSE (s.pendingIn().has_value());
    CHECK_FALSE (s.pendingTape().has_value());
}

TEST_CASE ("markIn from Armed sets the pending in-point and tape and transitions to AwaitingOut",
           "[capture-session]")
{
    CaptureSession s;
    s.arm();

    const bool accepted = s.markIn (Rational (5, 2), TapeId (3));
    CHECK (accepted);
    CHECK (s.state() == CaptureState::AwaitingOut);
    CHECK (s.isCapturing());
    CHECK (s.pendingIn() == std::optional<Rational> (Rational (5, 2)));
    CHECK (s.pendingTape() == std::optional<TapeId> (TapeId (3)));
}

TEST_CASE ("a second markIn replaces the pending in-point and stays in AwaitingOut",
           "[capture-session]")
{
    CaptureSession s;
    s.arm();
    REQUIRE (s.markIn (Rational (1), TapeId (2)));

    SECTION ("same tape, new in-point: tape is preserved, in-point is replaced")
    {
        const bool accepted = s.markIn (Rational (4), TapeId (2));
        CHECK (accepted);
        CHECK (s.state() == CaptureState::AwaitingOut);
        CHECK (s.pendingIn() == std::optional<Rational> (Rational (4)));
        CHECK (s.pendingTape() == std::optional<TapeId> (TapeId (2)));
    }

    SECTION ("different tape, new in-point: both are replaced together")
    {
        const bool accepted = s.markIn (Rational (4), TapeId (9));
        CHECK (accepted);
        CHECK (s.state() == CaptureState::AwaitingOut);
        CHECK (s.pendingIn() == std::optional<Rational> (Rational (4)));
        CHECK (s.pendingTape() == std::optional<TapeId> (TapeId (9)));
    }
}

TEST_CASE ("markOut from Disarmed or Armed returns nullopt without side effect",
           "[capture-session]")
{
    CaptureSession s;

    SECTION ("Disarmed")
    {
        const auto region = s.markOut (Rational (1));
        CHECK_FALSE (region.has_value());
        CHECK (s.state() == CaptureState::Disarmed);
    }

    SECTION ("Armed (no in-point yet)")
    {
        s.arm();
        const auto region = s.markOut (Rational (1));
        CHECK_FALSE (region.has_value());
        CHECK (s.state() == CaptureState::Armed);
    }
}

TEST_CASE ("markOut with t > pendingIn closes the region, stamps the tape, and returns to Armed",
           "[capture-session]")
{
    CaptureSession s;
    s.arm();
    REQUIRE (s.markIn (Rational (2), TapeId (11)));

    const auto region = s.markOut (Rational (7));
    REQUIRE (region.has_value());
    CHECK (region->tape           == TapeId (11));
    CHECK (region->inLmcSeconds  == Rational (2));
    CHECK (region->outLmcSeconds == Rational (7));

    // The session is ready for the next capture: Armed, no pending in-point or tape.
    CHECK (s.state() == CaptureState::Armed);
    CHECK_FALSE (s.pendingIn().has_value());
    CHECK_FALSE (s.pendingTape().has_value());
}

TEST_CASE ("markOut stamps the tape that was pinned by the *last* markIn",
           "[capture-session]")
{
    // The performer marks an in on track 2, then changes their mind and
    // re-marks on track 5 before committing. The closed region must carry
    // track 5 — the latest pinned tape — not track 2.
    CaptureSession s;
    s.arm();
    REQUIRE (s.markIn (Rational (1), TapeId (2)));
    REQUIRE (s.markIn (Rational (3), TapeId (5)));

    const auto region = s.markOut (Rational (8));
    REQUIRE (region.has_value());
    CHECK (region->tape          == TapeId (5));
    CHECK (region->inLmcSeconds  == Rational (3));
    CHECK (region->outLmcSeconds == Rational (8));
}

TEST_CASE ("markOut with t <= pendingIn returns nullopt and keeps AwaitingOut and pending tape",
           "[capture-session]")
{
    CaptureSession s;
    s.arm();
    REQUIRE (s.markIn (Rational (5), TapeId (4)));

    SECTION ("equal in and out is rejected")
    {
        const auto region = s.markOut (Rational (5));
        CHECK_FALSE (region.has_value());
        CHECK (s.state() == CaptureState::AwaitingOut);
        CHECK (s.pendingIn() == std::optional<Rational> (Rational (5)));
        CHECK (s.pendingTape() == std::optional<TapeId> (TapeId (4)));
    }

    SECTION ("out before in is rejected")
    {
        const auto region = s.markOut (Rational (3));
        CHECK_FALSE (region.has_value());
        CHECK (s.state() == CaptureState::AwaitingOut);
        CHECK (s.pendingIn() == std::optional<Rational> (Rational (5)));
        CHECK (s.pendingTape() == std::optional<TapeId> (TapeId (4)));
    }
}

TEST_CASE ("cancel from AwaitingOut returns to Armed and discards pending in-point and tape",
           "[capture-session]")
{
    CaptureSession s;
    s.arm();
    REQUIRE (s.markIn (Rational (4), TapeId (6)));

    s.cancel();
    CHECK (s.state() == CaptureState::Armed);
    CHECK_FALSE (s.pendingIn().has_value());
    CHECK_FALSE (s.pendingTape().has_value());
}

TEST_CASE ("cancel from Disarmed or Armed is a no-op", "[capture-session]")
{
    CaptureSession s;

    SECTION ("Disarmed")
    {
        s.cancel();
        CHECK (s.state() == CaptureState::Disarmed);
    }

    SECTION ("Armed")
    {
        s.arm();
        s.cancel();
        CHECK (s.state() == CaptureState::Armed);
    }
}

TEST_CASE ("a full capture cycle: arm, mark, mark, capture again",
           "[capture-session]")
{
    CaptureSession s;

    s.arm();
    REQUIRE (s.markIn (Rational (1), TapeId (1)));
    const auto first = s.markOut (Rational (5));
    REQUIRE (first.has_value());
    CHECK (first->tape          == TapeId (1));
    CHECK (first->inLmcSeconds  == Rational (1));
    CHECK (first->outLmcSeconds == Rational (5));
    CHECK (s.state() == CaptureState::Armed);

    REQUIRE (s.markIn (Rational (6), TapeId (2)));
    const auto second = s.markOut (Rational (11));
    REQUIRE (second.has_value());
    CHECK (second->tape          == TapeId (2));
    CHECK (second->inLmcSeconds  == Rational (6));
    CHECK (second->outLmcSeconds == Rational (11));
    CHECK (s.state() == CaptureState::Armed);
}
