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
}

bool CaptureSession::markIn (Rational t)
{
    if (state_ == CaptureState::Disarmed)
        return false;

    pendingIn_ = t;
    state_ = CaptureState::AwaitingOut;
    return true;
}

std::optional<CaptureRegion> CaptureSession::markOut (Rational t)
{
    if (state_ != CaptureState::AwaitingOut)
        return std::nullopt;

    // pendingIn_ is guaranteed to have a value when state_ == AwaitingOut.
    const Rational in = *pendingIn_;
    if (! (in < t))
        return std::nullopt;

    pendingIn_.reset();
    state_ = CaptureState::Armed;
    return CaptureRegion { in, t };
}

void CaptureSession::cancel()
{
    if (state_ == CaptureState::AwaitingOut)
    {
        pendingIn_.reset();
        state_ = CaptureState::Armed;
    }
}

} // namespace sirius
