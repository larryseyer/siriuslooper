#include "ida/otto/OttoHost.h"

#include <otto/manager/PlayerManager.h>
#include <otto/transport/TransportTracker.h>

namespace ida::otto
{

struct OttoHost::Impl
{
    ::otto::manager::PlayerManager playerManager;
    ::otto::TransportTracker       transportTracker;
};

OttoHost::OttoHost()
    : impl_ (std::make_unique<Impl>())
{
}

OttoHost::~OttoHost() = default;

void OttoHost::prepare (double sampleRate, int maxBlockSize)
{
    impl_->playerManager.prepare (sampleRate, maxBlockSize);
}

bool OttoHost::isPrepared() const noexcept
{
    return impl_->playerManager.isPrepared();
}

} // namespace ida::otto
