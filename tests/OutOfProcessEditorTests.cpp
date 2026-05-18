// Integration tests for the M7 S5 macOS GUI editor surface
// (OutOfProcessPluginInstance + OutOfProcessEffectChainHost editor API).
//
// Drives the synthetic CLAP plug-in's `clap_gui_cocoa` impl through the
// out-of-process pipeline and asserts the round-trip CAContextID
// publishing protocol works end-to-end:
//
//   1. requestEditorShow → host services → engine sees a non-zero
//      CAContextID + the synthetic plug-in's preferred size.
//   2. requestEditorHide → host tears down → engine sees CAContextID = 0.
//   3. Supervisor restart (SIGKILL the child while an editor is open) →
//      new child re-publishes a fresh CAContextID via the supervisor's
//      re-show path in attemptRestart.
//
// **Visual compositing not asserted.** ctest runs headless; JUCE's
// offscreen window doesn't reliably composite a CAContext-published
// layer. The IPC + lifecycle + restart re-publication contracts are what
// these cases exercise. Operator-launch eyes-on of the .app verifies
// the actual pixel path.
//
// macOS only. The synthetic plug-in's GUI extension is gated on
// `__APPLE__` and `sirius_gui_*` shims live only in host_process/
// gui_cocoa.mm; on other platforms these cases SKIP cleanly.
//
// Tag: `[plugin-editor]`.

#include "sirius/EffectChain.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/PluginDescriptor.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <thread>

namespace
{
    juce::File hostBinary()
    {
       #ifdef SIRIUS_PLUGIN_HOST_PATH
        return juce::File (SIRIUS_PLUGIN_HOST_PATH);
       #else
        return juce::File();
       #endif
    }

    juce::File clapBundle()
    {
       #ifdef SIRIUS_SYNTHETIC_CLAP_PATH
        return juce::File (SIRIUS_SYNTHETIC_CLAP_PATH);
       #else
        return juce::File();
       #endif
    }

    /// Configures a single-slot bus + waits for the host to dlopen +
    /// activate the CLAP. Mirrors the M7 S4 supervisor-tests helper.
    void primeHost (sirius::OutOfProcessEffectChainHost& host,
                    std::int64_t busId,
                    const juce::File& binary,
                    const juce::File& bundle)
    {
        sirius::PluginDescriptor descriptor;
        descriptor.format   = sirius::PluginFormat::Clap;
        descriptor.name     = "SyntheticTestPlugin";
        descriptor.filePath = bundle.getFullPathName().toStdString();

        sirius::EffectChainEntry entry;
        entry.descriptor  = descriptor;
        entry.displayName = "Identity";

        sirius::EffectChain chain;
        chain = chain.withAppended (entry);

        host.configureBus (busId, chain, binary, bundle);
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
    }

    /// Polls `editorCaContextId(busId, slot)` up to `timeoutMs`; returns
    /// the first non-zero value it sees, or 0 on timeout. Polling interval
    /// is short (10 ms) since the host services GUI inside its CLAP pump
    /// loop's idle slot — round-trip should land well under 100 ms.
    std::uint32_t waitForContextId (sirius::OutOfProcessEffectChainHost& host,
                                    std::int64_t busId, std::size_t slotIdx,
                                    int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds (timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto id = host.editorCaContextId (busId, slotIdx);
            if (id != 0) return id;
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        return 0;
    }

    /// Polls until `editorCaContextId` returns 0, or timeout. Used to
    /// confirm a Hide request was serviced.
    bool waitForContextZero (sirius::OutOfProcessEffectChainHost& host,
                             std::int64_t busId, std::size_t slotIdx,
                             int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds (timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (host.editorCaContextId (busId, slotIdx) == 0)
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        return false;
    }
}

#ifdef __APPLE__

TEST_CASE ("editor Show round-trip publishes a non-zero CAContextID",
           "[plugin-editor]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("sirius_plugin_host binary not present at SIRIUS_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at SIRIUS_SYNTHETIC_CLAP_PATH");

    sirius::OutOfProcessEffectChainHost host;
    primeHost (host, /* busId */ 10, binary, bundle);

    REQUIRE (host.requestEditorShow (10, 0, /* w */ 200, /* h */ 100));

    const auto contextId = waitForContextId (host, 10, 0, /* timeoutMs */ 1500);
    CHECK (contextId != 0u);

    const auto sz = host.editorSize (10, 0);
    CHECK (sz.first  == 200u);
    CHECK (sz.second == 100u);
}

TEST_CASE ("editor Hide releases the CAContextID",
           "[plugin-editor]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("sirius_plugin_host binary not present at SIRIUS_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at SIRIUS_SYNTHETIC_CLAP_PATH");

    sirius::OutOfProcessEffectChainHost host;
    primeHost (host, /* busId */ 11, binary, bundle);

    REQUIRE (host.requestEditorShow (11, 0, 200, 100));
    REQUIRE (waitForContextId (host, 11, 0, 1500) != 0u);

    REQUIRE (host.requestEditorHide (11, 0));
    CHECK (waitForContextZero (host, 11, 0, /* timeoutMs */ 1500));
}

TEST_CASE ("supervisor restart re-publishes CAContextID after SIGKILL",
           "[plugin-editor]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("sirius_plugin_host binary not present at SIRIUS_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at SIRIUS_SYNTHETIC_CLAP_PATH");

    sirius::OutOfProcessEffectChainHost host;
    primeHost (host, /* busId */ 12, binary, bundle);

    REQUIRE (host.requestEditorShow (12, 0, 200, 100));
    const auto originalContextId = waitForContextId (host, 12, 0, 1500);
    REQUIRE (originalContextId != 0u);

    // Drive a few pump cycles before SIGKILL so the watchdog starts from
    // a known-good baseline (same priming pattern as the supervisor tests).
    constexpr int kBlock = 64;
    std::array<float, kBlock> left  {};
    std::array<float, kBlock> right {};
    std::array<float, kBlock> outL  {};
    std::array<float, kBlock> outR  {};
    left.fill (0.1f);
    const std::array<const float*, 2> inPtrs  { left.data(),  right.data() };
    const std::array<float*, 2>       outPtrs { outL.data(),  outR.data() };
    for (int i = 0; i < 5; ++i)
    {
        host.pumpSlot (12, 0, inPtrs.data(),
                       const_cast<float* const*> (outPtrs.data()), 2, kBlock);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    REQUIRE (host.restartCountForTesting (12, 0) == 0u);

    // SIGKILL the host child. Drive pumps to accumulate misses past the
    // watchdog threshold; supervisor will respawn.
    const long pid = host.childPidForTestingAtSlot (12, 0);
    REQUIRE (pid > 0);
    REQUIRE (::kill (static_cast<pid_t> (pid), SIGKILL) == 0);

    // Drive enough pumps to cross the miss threshold (16 by S4 default).
    for (int i = 0; i < 64; ++i)
    {
        host.pumpSlot (12, 0, inPtrs.data(),
                       const_cast<float* const*> (outPtrs.data()), 2, kBlock);
        std::this_thread::sleep_for (std::chrono::milliseconds (3));
    }

    // Wait for the supervisor to escalate restart + child respawn + GUI
    // re-show. Per S4 the supervisor polls every 50ms and the restart
    // grace is 100ms; respawn + dlopen latency adds another ~300ms; GUI
    // re-issue + service is fast (one CLAP pump iteration).
    const auto restartDeadline = std::chrono::steady_clock::now()
                               + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now() < restartDeadline)
    {
        if (host.restartCountForTesting (12, 0) >= 1u)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    REQUIRE (host.restartCountForTesting (12, 0) >= 1u);

    const auto newContextId = waitForContextId (host, 12, 0, /* timeoutMs */ 2000);
    CHECK (newContextId != 0u);
    // The new CAContextID may or may not equal the old one — it's
    // process-scoped on the child side and the child is a different
    // process now. Either way, what matters is that the publish protocol
    // delivered a fresh non-zero handle after the restart.
}

#endif // __APPLE__
