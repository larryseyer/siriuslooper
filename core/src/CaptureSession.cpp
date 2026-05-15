#include "sirius/CaptureSession.h"

namespace sirius
{

void CaptureSession::arm()
{
    if (state_ == CaptureState::Disarmed)
        state_ = CaptureState::Armed;
}

void CaptureSession::disarm()
{
    state_ = CaptureState::Disarmed;
    pendingIn_.reset();
    pendingTape_.reset();
}

bool CaptureSession::markIn (Rational t, TapeId tape)
{
    if (state_ == CaptureState::Disarmed)
        return false;

    pendingIn_ = t;
    pendingTape_ = tape;
    state_ = CaptureState::AwaitingOut;
    return true;
}

std::optional<CaptureRegion> CaptureSession::markOut (Rational t)
{
    if (state_ != CaptureState::AwaitingOut)
        return std::nullopt;

    // Both pendingIn_ and pendingTape_ are guaranteed to have values when
    // state_ == AwaitingOut — they are set together in markIn and cleared
    // together in disarm / cancel / a successful markOut.
    const Rational in = *pendingIn_;
    const TapeId tape = *pendingTape_;
    if (! (in < t))
        return std::nullopt;

    pendingIn_.reset();
    pendingTape_.reset();
    state_ = CaptureState::Armed;
    return CaptureRegion { tape, in, t };
}

void CaptureSession::cancel()
{
    if (state_ == CaptureState::AwaitingOut)
    {
        pendingIn_.reset();
        pendingTape_.reset();
        state_ = CaptureState::Armed;
    }
}

} // namespace sirius
