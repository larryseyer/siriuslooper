#include "sirius/DirectLayer.h"

#include <cassert>

namespace sirius
{

DirectLayer::DirectLayer()  = default;
DirectLayer::~DirectLayer() = default;

DirectLayer::RouteId DirectLayer::addRawRoute (InputId source, OutputChannelId destination)
{
    const auto gen = nextRawGeneration_++;
    rawRoutes_.push_back (RawRoute { source, destination, gen });
    return RouteId (RouteId::Kind::Raw, gen);
}

DirectLayer::RouteId DirectLayer::addProcessedRoute (ChannelId source, OutputChannelId destination)
{
    const auto gen = nextProcessedGeneration_++;
    processedRoutes_.push_back (ProcessedRoute { source, destination, gen });
    return RouteId (RouteId::Kind::Processed, gen);
}

void DirectLayer::removeRoute (RouteId id)
{
    switch (id.kind_)
    {
        case RouteId::Kind::Raw:
        {
            for (std::size_t i = 0; i < rawRoutes_.size(); ++i)
            {
                if (rawRoutes_[i].generation == id.generation_)
                {
                    rawRoutes_[i] = rawRoutes_.back();
                    rawRoutes_.pop_back();
                    return;
                }
            }
            assert (false && "DirectLayer::removeRoute: raw RouteId not found (double-remove or forged handle)");
            return;
        }
        case RouteId::Kind::Processed:
        {
            for (std::size_t i = 0; i < processedRoutes_.size(); ++i)
            {
                if (processedRoutes_[i].generation == id.generation_)
                {
                    processedRoutes_[i] = processedRoutes_.back();
                    processedRoutes_.pop_back();
                    return;
                }
            }
            assert (false && "DirectLayer::removeRoute: processed RouteId not found (double-remove or forged handle)");
            return;
        }
    }
}

std::size_t DirectLayer::rawRouteCount() const noexcept
{
    return rawRoutes_.size();
}

std::size_t DirectLayer::processedRouteCount() const noexcept
{
    return processedRoutes_.size();
}

} // namespace sirius
