// =============================================================================
// PluginGuiBridge.cpp — engine-side XPC bridge holder skeleton (M7 S6).
// =============================================================================
// Task 1 lands the null-impl + test seam. Task 3 swaps the null path for
// the real XPC connection (Apple-only, via the .mm companion).
// =============================================================================

#include "sirius/PluginGuiBridge.h"

#include <atomic>

namespace sirius
{
namespace
{
    struct NullGuiBridge : public IGuiBridge
    {
        bool isReady() const noexcept override { return false; }
        void registerServerPort (std::uint32_t) noexcept override {}
    };

    NullGuiBridge          g_nullBridge {};
    std::atomic<IGuiBridge*> g_injected { nullptr };
}

IGuiBridge& PluginGuiBridge::instance() noexcept
{
    if (auto* injected = g_injected.load (std::memory_order_acquire))
        return *injected;
    return g_nullBridge;
}

void PluginGuiBridge::setInstanceForTesting (IGuiBridge* bridge) noexcept
{
    g_injected.store (bridge, std::memory_order_release);
}

void PluginGuiBridge::resetForTesting() noexcept
{
    g_injected.store (nullptr, std::memory_order_release);
}

} // namespace sirius
