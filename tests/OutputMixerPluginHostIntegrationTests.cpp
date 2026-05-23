// M7 Session 3 integration test — `Bus::process` → `OutOfProcessEffectChainHost`
// → real `ida_plugin_host` CLAP child → `SyntheticTestPlugin` identity
// plug-in → back through the SPSC rings → `Bus::process` writes the result
// into `output`. This is the first session that puts the IPC layer on the
// audio call chain; this test is the load-bearing audit of that wiring.
//
// Acceptance per the plan: drive `Bus::process` N times against a stereo
// configuration where buffer k is filled with the constant value `(k+1)`,
// and assert the pipelined 1-buffer delay model:
//   - output[0] == dry mix (since the host has nothing to pop yet)
//   - output[k] (k >= 1) carries the previous buffer's audio
// The synthetic CLAP is identity, so the previous buffer's audio is exactly
// what we pushed.
//
// Tag: `[output-mixer][plugin-host]`. The test SKIPs cleanly if either the
// host binary or the .clap bundle is not built — the orchestrator's CMake
// `add_dependencies(IdaTests ida_plugin_host SyntheticTestPlugin)`
// makes this path the norm rather than the exception, but the SKIP keeps
// the suite green under partial-build configurations.

#include "sirius/Bus.h"
#include "sirius/Channel.h"
#include "sirius/EffectChain.h"
#include "sirius/OutOfProcessEffectChainHost.h"
#include "sirius/PluginDescriptor.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
    juce::File hostBinary()
    {
       #ifdef IDA_PLUGIN_HOST_PATH
        return juce::File (IDA_PLUGIN_HOST_PATH);
       #else
        return juce::File();
       #endif
    }

    juce::File clapBundle()
    {
       #ifdef IDA_SYNTHETIC_CLAP_PATH
        return juce::File (IDA_SYNTHETIC_CLAP_PATH);
       #else
        return juce::File();
       #endif
    }
}

TEST_CASE ("Bus::process pipelines stereo buffers through OutOfProcessEffectChainHost + identity CLAP",
           "[output-mixer][plugin-host]")
{
    const auto binary = hostBinary();
    if (! binary.existsAsFile())
        SKIP ("ida_plugin_host binary not present at IDA_PLUGIN_HOST_PATH");

    const auto bundle = clapBundle();
   #ifdef __APPLE__
    if (! bundle.isDirectory())
        SKIP ("SyntheticTestPlugin .clap bundle not present at IDA_SYNTHETIC_CLAP_PATH");
   #else
    if (! bundle.existsAsFile())
        SKIP ("SyntheticTestPlugin .clap shared library not present at IDA_SYNTHETIC_CLAP_PATH");
   #endif

    // Build a chain with one non-bypassed slot pointing at the synthetic
    // CLAP. The descriptor is illustrative — the host spawns the same
    // bundle for every active slot in S3 (single-plug-in simplification
    // per the OutOfProcessEffectChainHost docblock).
    ida::PluginDescriptor descriptor;
    descriptor.format   = ida::PluginFormat::Clap;
    descriptor.name     = "SyntheticTestPlugin";
    descriptor.filePath = bundle.getFullPathName().toStdString();

    ida::EffectChainEntry entry;
    entry.descriptor  = descriptor;
    entry.displayName = "Identity";

    ida::EffectChain chain;
    chain = chain.withAppended (entry);

    // Wire the host + bus. configureBus is message-thread (matches the M5/
    // M6 collaborator contract — runs to completion before any audio-
    // thread call into pumpSlot).
    ida::OutOfProcessEffectChainHost host;
    host.configureBus (1, chain, binary, bundle);

    ida::Bus bus (ida::BusId { 1 }, ida::BusConfig { 2, "FxAux" });
    bus.setEffectChain (chain);
    bus.setEffectChainHost (&host);

    // Give the CLAP child a brief window to dlopen + activate before we
    // start pushing audio. Without this, the first few buffers' worth of
    // payloads can queue up before the host pulls — still correct (the
    // SPSC ring buffers them) but slows the test's pipelined assertion.
    // NOT a band-aid for any pumpSlot race; the SPSC contract guarantees
    // correctness without the sleep. Same rationale as the per-iteration
    // sleep below — host wake-up latency, not engine-side determinism.
    std::this_thread::sleep_for (std::chrono::milliseconds (250));

    constexpr std::size_t kBufferCount = 8;
    constexpr std::size_t kBlockSize   = 64;

    // Record the input pattern we'll push per buffer (k=0 → 1.0, k=1 →
    // 2.0, …). Per-channel: left = (k+1), right = -(k+1) so a swapped-
    // channels bug would fail the assertion below.
    std::array<float, kBufferCount> inputLeftValues  {};
    std::array<float, kBufferCount> inputRightValues {};
    for (std::size_t k = 0; k < kBufferCount; ++k)
    {
        inputLeftValues [k] =  static_cast<float> (k + 1);
        inputRightValues[k] = -static_cast<float> (k + 1);
    }

    // Capture each buffer's output for post-loop assertion.
    std::vector<std::array<float, kBlockSize>> capturedLeft (kBufferCount);
    std::vector<std::array<float, kBlockSize>> capturedRight (kBufferCount);

    for (std::size_t k = 0; k < kBufferCount; ++k)
    {
        // Populate the bus's mixBuffer for this buffer. This is the
        // audio-thread input — OutputMixer's Step 2 (send-matrix
        // accumulate) would normally fill this; the test stands in for
        // it directly to keep the wiring minimal.
        float* const busLeft  = bus.mixBufferChannel (0);
        float* const busRight = bus.mixBufferChannel (1);
        REQUIRE (busLeft  != nullptr);
        REQUIRE (busRight != nullptr);
        for (std::size_t s = 0; s < kBlockSize; ++s)
        {
            busLeft [s] = inputLeftValues [k];
            busRight[s] = inputRightValues[k];
        }

        // Fresh zeroed output per buffer — the assertion below checks
        // that Bus::process either left the output at 0 (no response
        // yet → caller's dry-on-miss; but Bus::process's chain path
        // additively accumulates `processedBuffer_`, which carries the
        // dry mix from before the chain ran, so the FIRST buffer ALSO
        // sees the input on the output — see assertions below).
        std::array<float, kBlockSize> outLeft  {};
        std::array<float, kBlockSize> outRight {};
        outLeft .fill (0.0f);
        outRight.fill (0.0f);
        std::array<float*, 2> outputPtrs { outLeft.data(), outRight.data() };

        bus.process (outputPtrs.data(), 2, static_cast<int> (kBlockSize));

        capturedLeft [k] = outLeft;
        capturedRight[k] = outRight;

        // Sleep briefly between iterations so the host child has time to
        // process the buffer we just pushed and put the response on the
        // host→engine ring before the next iteration's pop. The pipelined
        // model tolerates a miss — but the test wants determinism, so the
        // sleep removes the schedule-dependent flake.
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    // Pipelined 1-buffer delay assertion:
    //
    //   Buffer 0: pumpSlot writes input 0 to the ring, tries to pop a
    //   response — ring is empty, pop returns false. Bus::process's
    //   processedBuffer_ still holds the dry mix from the pre-pump copy
    //   (input 0). Bus::process then additively sums processedBuffer_
    //   into the zeroed output → output 0 == input 0. **First buffer
    //   carries dry signal**, not silence.
    //
    //   Buffer k (k >= 1): pumpSlot writes input k to the ring, pops the
    //   response for input k-1 from the ring, de-interleaves it into
    //   processedBuffer_, then Bus::process additively sums that into
    //   zeroed output → output k == input k-1 (since the synthetic CLAP
    //   is identity).
    //
    // Tight epsilon because identity CLAP is bit-exact under float.

    // Buffer 0 — dry mix (pre-pump copy survived the empty pop).
    for (std::size_t s = 0; s < kBlockSize; ++s)
    {
        CHECK (capturedLeft [0][s] == Catch::Approx (inputLeftValues [0]).margin (1e-5f));
        CHECK (capturedRight[0][s] == Catch::Approx (inputRightValues[0]).margin (1e-5f));
    }

    // Buffers 1..N — pipelined response = previous input.
    for (std::size_t k = 1; k < kBufferCount; ++k)
    {
        for (std::size_t s = 0; s < kBlockSize; ++s)
        {
            CHECK (capturedLeft [k][s] == Catch::Approx (inputLeftValues [k - 1]).margin (1e-5f));
            CHECK (capturedRight[k][s] == Catch::Approx (inputRightValues[k - 1]).margin (1e-5f));
        }
    }
}
