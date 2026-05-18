// Tests for sirius::InputMixer — real-body coverage added in M3 Session 2.
// The M2 stubs have been replaced with real implementations; these tests
// verify channel registration, tape-bearing buffer dispatch, and overload
// reporting on queue-full.
//
// Note: TapeWriter takes std::chrono::milliseconds for the flush interval;
// the caller converts from CapabilityTier before constructing.
#include "sirius/InputMixer.h"
#include "sirius/OverloadProtection.h"
#include "sirius/TapeWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <type_traits>

using sirius::InputMixer;

static_assert (std::is_default_constructible_v<InputMixer>,
               "InputMixer must remain default-constructible");
static_assert (std::is_destructible_v<InputMixer>);

TEST_CASE ("InputMixer is default-constructible and destructible without crashing",
           "[input-mixer]")
{
    InputMixer mixer;
    (void) mixer;
}

TEST_CASE ("InputMixer::processBuffer enqueues one message per tape-bearing channel",
           "[input-mixer][process-buffer]")
{
    using sirius::ChannelId;
    using sirius::InputId;
    using sirius::OverloadProtection;
    using sirius::SignalType;
    using sirius::TapeMode;
    using sirius::TapeWriter;

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-inputmixer-process-"
                                      + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDir.createDirectory();

    // 1 ms flush interval (Lavish-equivalent)
    TapeWriter writer (tempDir, std::chrono::milliseconds (1), 64);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);

    const auto chCommit = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (chCommit, TapeMode::CommitToTape);

    const auto chNoTape = mixer.addChannel (InputId (1), SignalType::Audio);
    mixer.setChannelTapeMode (chNoTape, TapeMode::NoTape);

    std::array<std::byte, 64> buffer {};
    for (auto& b : buffer) b = std::byte { 0x7E };
    mixer.processBuffer (chCommit, buffer.data(), buffer.size());
    mixer.processBuffer (chNoTape, buffer.data(), buffer.size());

    // Finalize the CommitToTape channel and verify its partial holds the bytes.
    const auto partial = writer.flushChannel (chCommit);
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 64);

    // NoTape channel must not have written anything.
    const auto notapeFile = tempDir.getChildFile (
        juce::String (chNoTape.value()) + ".tape.partial");
    CHECK_FALSE (notapeFile.existsAsFile());

    tempDir.deleteRecursively();
}

TEST_CASE ("InputMixer::processBuffer reports overload when the writer queue is full",
           "[input-mixer][overload]")
{
    using sirius::InputId;
    using sirius::OverloadProtection;
    using sirius::SignalType;
    using sirius::TapeMode;
    using sirius::TapeWriter;

    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-inputmixer-overload-"
                                      + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDir.createDirectory();

    // 1000 ms flush interval (Survival-equivalent) — slow flush keeps queue full
    TapeWriter writer (tempDir, std::chrono::milliseconds (1000), 2);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);

    std::array<std::byte, 16> buffer {};
    // 5 pushes against a capacity-2 queue: 3 will be dropped + report overload.
    for (int i = 0; i < 5; ++i)
        mixer.processBuffer (ch, buffer.data(), buffer.size());

    CHECK (overload.lastReportedLoad() == 1.0);

    tempDir.deleteRecursively();
}
