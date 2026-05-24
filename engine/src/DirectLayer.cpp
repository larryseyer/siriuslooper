#include "ida/DirectLayer.h"

#include <algorithm>
#include <cassert>

namespace ida
{

namespace
{

// Linear scan over the buffer views supplied to routeBuffers. Bounded by
// the (small) buffer-count argument from AudioCallback; cheap, branch-light,
// and allocation-free. Returns nullptr on miss so routeBuffers can skip
// the route silently.
const float* findRawInput (std::span<const RawInputBufferView> rawInputs,
                           InputId                              id,
                           int&                                 outSampleCount) noexcept
{
    for (const auto& view : rawInputs)
    {
        if (view.id == id)
        {
            outSampleCount = view.sampleCount;
            return view.samples;
        }
    }
    outSampleCount = 0;
    return nullptr;
}

const float* findProcessed (std::span<const ProcessedChannelBufferView> processed,
                            ChannelId                                    id,
                            int&                                         outSampleCount) noexcept
{
    for (const auto& view : processed)
    {
        if (view.id == id)
        {
            outSampleCount = view.sampleCount;
            return view.samples;
        }
    }
    outSampleCount = 0;
    return nullptr;
}

float* findOutput (std::span<const OutputBufferView> outputs,
                   OutputChannelId                    id,
                   int&                               outSampleCount) noexcept
{
    for (const auto& view : outputs)
    {
        if (view.id == id)
        {
            outSampleCount = view.sampleCount;
            return view.samples;
        }
    }
    outSampleCount = 0;
    return nullptr;
}

} // namespace

DirectLayer::DirectLayer()  = default;
DirectLayer::~DirectLayer() = default;

DirectLayer::RouteId DirectLayer::addRawRoute (InputId source, OutputChannelId destination,
                                               const std::atomic<bool>* muteFlag)
{
    const auto gen = nextRawGeneration_++;
    rawRoutes_.push_back (RawRoute { source, destination, gen, muteFlag });
    return RouteId (RouteId::Kind::Raw, gen);
}

DirectLayer::RouteId DirectLayer::addProcessedRoute (ChannelId source, OutputChannelId destination,
                                                     const std::atomic<bool>* muteFlag)
{
    const auto gen = nextProcessedGeneration_++;
    processedRoutes_.push_back (ProcessedRoute { source, destination, gen, muteFlag });
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
        default: assert (false && "DirectLayer::removeRoute — unknown RouteId::Kind"); return;
    }
}

void DirectLayer::routeBuffers (std::span<const RawInputBufferView>         rawInputs,
                                std::span<const ProcessedChannelBufferView> processedChannels,
                                std::span<const OutputBufferView>           outputs) const noexcept
{
    for (const auto& route : rawRoutes_)
    {
        // Operator's mute on the source channel is a kill-switch for the
        // monitor signal too — even on Raw routes that tap pre-strip. One
        // relaxed atomic read per route per block; bounded by route count.
        if (route.muteFlag != nullptr && route.muteFlag->load (std::memory_order_relaxed))
            continue;

        int          srcCount = 0;
        int          dstCount = 0;
        const float* src      = findRawInput (rawInputs, route.source,      srcCount);
        float*       dst      = findOutput   (outputs,   route.destination, dstCount);

        if (src == nullptr || dst == nullptr)
            continue;

        const int n = std::min (srcCount, dstCount);
        for (int i = 0; i < n; ++i)
            dst[i] += src[i];
    }

    for (const auto& route : processedRoutes_)
    {
        if (route.muteFlag != nullptr && route.muteFlag->load (std::memory_order_relaxed))
            continue;

        int          srcCount = 0;
        int          dstCount = 0;
        const float* src      = findProcessed (processedChannels, route.source,      srcCount);
        float*       dst      = findOutput    (outputs,           route.destination, dstCount);

        if (src == nullptr || dst == nullptr)
            continue;

        const int n = std::min (srcCount, dstCount);
        for (int i = 0; i < n; ++i)
            dst[i] += src[i];
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

} // namespace ida
