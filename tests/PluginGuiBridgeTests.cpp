// Unit tests for the engine-side XPC bridge connection holder (M7 S6).
// Drives PluginGuiBridge through the IGuiBridge stub seam so XPC + Cocoa
// are not required (CI ctest runs from build/ with no .app bundle).
//
// Tag: [plugin-editor-xpc][unit]

#include "sirius/IGuiBridge.h"
#include "sirius/PluginGuiBridge.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>

namespace
{
    struct StubBridge : public sirius::IGuiBridge
    {
        std::atomic<int>           registerCalls { 0 };
        std::atomic<std::uint32_t> lastPortName  { 0 };
        std::atomic<bool>          readyFlag     { true };

        bool isReady() const noexcept override { return readyFlag.load(); }

        void registerServerPort (std::uint32_t portName) noexcept override
        {
            registerCalls.fetch_add (1);
            lastPortName.store (portName);
        }
    };
}

TEST_CASE ("PluginGuiBridge defaults to NullGuiBridge when no instance injected",
           "[plugin-editor-xpc][unit]")
{
    sirius::PluginGuiBridge::resetForTesting();
    auto& bridge = sirius::PluginGuiBridge::instance();
    CHECK_FALSE (bridge.isReady()); // NullGuiBridge is never ready
    bridge.registerServerPort (42u); // must not throw
}

TEST_CASE ("PluginGuiBridge::setInstanceForTesting routes calls to the stub",
           "[plugin-editor-xpc][unit]")
{
    StubBridge stub;
    sirius::PluginGuiBridge::setInstanceForTesting (&stub);
    auto& bridge = sirius::PluginGuiBridge::instance();
    CHECK (bridge.isReady());

    bridge.registerServerPort (1234u);
    CHECK (stub.registerCalls.load() == 1);
    CHECK (stub.lastPortName.load() == 1234u);

    sirius::PluginGuiBridge::resetForTesting();
}

TEST_CASE ("PluginGuiBridge::resetForTesting clears injected instance",
           "[plugin-editor-xpc][unit]")
{
    StubBridge stub;
    sirius::PluginGuiBridge::setInstanceForTesting (&stub);
    sirius::PluginGuiBridge::resetForTesting();
    auto& bridge = sirius::PluginGuiBridge::instance();
    CHECK_FALSE (bridge.isReady());
}
