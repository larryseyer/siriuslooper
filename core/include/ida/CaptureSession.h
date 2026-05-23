#pragma once

#include "ida/Rational.h"
#include "ida/TapeId.h"

#include <optional>

namespace sirius
{

/// The performer-facing capture state. The IDA tape is always running
/// (white paper Part 7) — that architectural commitment is what enables
/// retroactive capture. CaptureSession sits *above* the tape and gates
/// whether the performer's marking gestures take effect, and (downstream)
/// whether captured tape data is preserved or discarded.
///
/// Three states:
/// - **Disarmed:** the performer has explicitly stood the system down. No
///   in/out marks land while disarmed. Default on construction so the
///   system starts in the safer state — nothing is captured by surprise.
/// - **Armed:** the system is ready; the tape is being captured; no
///   in-point is set.
/// - **AwaitingOut:** an in-point has been set and the system is waiting
///   for the corresponding out-point.
enum class CaptureState
{
    Disarmed,
    Armed,
    AwaitingOut
};

/// A closed capture region — the bookmark pair produced by a successful
/// markIn / markOut sequence. The performer (or a higher-level subsystem)
/// turns this into a Loop or Slice Constituent referencing the tape that
/// was running between the two timestamps. The `tape` field names *which*
/// input tape the region came from — pinned at markIn time and carried
/// through to markOut.
struct CaptureRegion
{
    TapeId tape;
    Rational inLmcSeconds;
    Rational outLmcSeconds;
};

/// A pure, JUCE-free state machine for the arm / mark-in / mark-out
/// gestures. Caller supplies LMC times; the session itself owns no clock.
/// All transitions are deterministic and idempotent on no-ops, so it is
/// safe to drive from any thread — wrap with the caller's preferred
/// synchronisation when crossing threads.
class CaptureSession
{
public:
    CaptureSession() = default;

    CaptureState state() const noexcept { return state_; }

    /// True in Armed or AwaitingOut. Performer-facing "is the system live?"
    bool isArmed() const noexcept { return state_ != CaptureState::Disarmed; }

    /// True in AwaitingOut. Performer-facing "is a capture in progress?"
    bool isCapturing() const noexcept { return state_ == CaptureState::AwaitingOut; }

    /// The current pending in-point, if one is set. nullopt outside
    /// AwaitingOut.
    const std::optional<Rational>& pendingIn() const noexcept { return pendingIn_; }

    /// Arm the system. From Disarmed, transitions to Armed. From any
    /// other state, no-op (idempotent).
    void arm();

    /// Disarm the system. From any state, transitions to Disarmed. Any
    /// pending in-point is discarded — disarming is a hard stand-down.
    void disarm();

    /// The current pending tape, if one is set. nullopt outside
    /// AwaitingOut.
    const std::optional<TapeId>& pendingTape() const noexcept { return pendingTape_; }

    /// Mark an in-point at LMC time `t` on tape `tape`. From Armed,
    /// transitions to AwaitingOut. From AwaitingOut, *replaces* both the
    /// pending in-point and pending tape (the performer may slide the
    /// in-point and may also switch tracks before committing). From
    /// Disarmed, ignored. Returns true iff the in-point was accepted.
    bool markIn (Rational t, TapeId tape);

    /// Mark an out-point at LMC time `t`. From AwaitingOut with t >
    /// pendingIn, transitions to Armed and returns the closed
    /// CaptureRegion. From AwaitingOut with t <= pendingIn, returns
    /// nullopt and stays in AwaitingOut (the performer can try again).
    /// From Disarmed or Armed, returns nullopt without side effect.
    std::optional<CaptureRegion> markOut (Rational t);

    /// Cancel a pending capture. From AwaitingOut, transitions to Armed
    /// and discards the pending in-point. Otherwise no-op.
    void cancel();

private:
    CaptureState state_ { CaptureState::Disarmed };
    std::optional<Rational> pendingIn_;
    std::optional<TapeId> pendingTape_;
};

} // namespace sirius
