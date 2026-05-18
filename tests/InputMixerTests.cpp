// Tests for sirius::InputMixer — real-body coverage added in M3 Session 2.
// The M2 stubs have been replaced with real implementations; these tests
// verify channel registration, tape-bearing buffer dispatch, and overload
// reporting on queue-full.
//
// Note: TapeWriter takes std::chrono::milliseconds for the flush interval;
// the caller converts from CapabilityTier before constructing.
#include "sirius/InputMixer.h"
#include "sirius/OverloadProtection.h"
#include "sirius/TapeStore.h"
#include "sirius/TapeWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <filesystem>
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

    const auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("sirius-inputmixer-process-"
                                                + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDirJuce.createDirectory();
    const std::filesystem::path tempDir (tempDirJuce.getFullPathName().toStdString());

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
    const juce::File partial (juce::String (writer.flushChannel (chCommit).string()));
    REQUIRE (partial.existsAsFile());
    CHECK (partial.getSize() == 64);

    // NoTape channel must not have written anything.
    const juce::File notapeFile (juce::String (
        (tempDir / (std::to_string (chNoTape.value()) + ".tape.partial")).string()));
    CHECK_FALSE (notapeFile.existsAsFile());

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}

TEST_CASE ("InputMixer::processBuffer reports overload when the writer queue is full",
           "[input-mixer][overload]")
{
    using sirius::InputId;
    using sirius::OverloadProtection;
    using sirius::SignalType;
    using sirius::TapeMode;
    using sirius::TapeWriter;

    const auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("sirius-inputmixer-overload-"
                                                + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDirJuce.createDirectory();
    const std::filesystem::path tempDir (tempDirJuce.getFullPathName().toStdString());

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

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}

TEST_CASE ("InputMixer::finalizeChannel produces a content-addressed tape and clears the partial",
           "[input-mixer][finalize]")
{
    using namespace sirius;
    using sirius::persistence::TapeStore;

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("sirius-finalize-"
                                   + juce::String (juce::Time::getMillisecondCounterHiRes()));
    root.createDirectory();
    auto partials = root.getChildFile ("partials"); partials.createDirectory();
    auto storeDir = root.getChildFile ("store");    storeDir.createDirectory();

    TapeStore store (storeDir);
    TapeWriter writer (std::filesystem::path (partials.getFullPathName().toStdString()),
                       std::chrono::milliseconds (1), 64);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    mixer.setTapeStore (&store);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);

    std::array<std::byte, 32> buffer {};
    for (auto& b : buffer) b = std::byte { 0x42 };
    mixer.processBuffer (ch, buffer.data(), buffer.size());

    mixer.finalizeChannel (ch);

    // Partial must be gone.
    const auto partial = partials.getChildFile (juce::String (ch.value()) + ".tape.partial");
    CHECK_FALSE (partial.existsAsFile());

    // Store must hold exactly one file whose bytes match.
    juce::Array<juce::File> stored;
    storeDir.findChildFiles (stored, juce::File::findFiles, false);
    REQUIRE (stored.size() == 1);
    juce::MemoryBlock bytes;
    REQUIRE (stored[0].loadFileAsData (bytes));
    CHECK (bytes.getSize() == 32);
    for (std::size_t i = 0; i < 32; ++i)
        CHECK (static_cast<unsigned char> (bytes[i]) == 0x42);

    root.deleteRecursively();
}

TEST_CASE ("NonDestructive channel writes both audio partial and JSONL params partial",
           "[input-mixer][non-destructive]")
{
    using namespace sirius;
    using sirius::persistence::TapeStore;

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("sirius-nondestructive-"
                                   + juce::String (juce::Time::getMillisecondCounterHiRes()));
    root.createDirectory();
    auto partials = root.getChildFile ("partials"); partials.createDirectory();
    auto storeDir = root.getChildFile ("store");    storeDir.createDirectory();

    TapeStore store (storeDir);
    TapeWriter writer (std::filesystem::path (partials.getFullPathName().toStdString()),
                       std::chrono::milliseconds (1), 64);
    OverloadProtection overload;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    mixer.setTapeStore (&store);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::NonDestructive);

    std::array<std::byte, 16> buffer {};
    mixer.processBuffer (ch, buffer.data(), buffer.size());

    // Drain so both files settle on disk.
    (void) writer.flushChannel (ch);

    const auto audioPartial  = partials.getChildFile (juce::String (ch.value()) + ".tape.partial");
    const auto paramsPartial = partials.getChildFile (juce::String (ch.value()) + ".params.partial");

    CHECK (audioPartial.existsAsFile());
    CHECK (paramsPartial.existsAsFile());

    // M3 ships an empty event stream for NonDestructive (Audio chains are
    // no-op in M3 — see M3 spec §"What 'dry' means in M3"). M5's real Audio
    // DSP earns the first events.
    CHECK (paramsPartial.getSize() == 0);

    root.deleteRecursively();
}

TEST_CASE ("addChannel honors the per-input default TapeMode set via setInputDefaults",
           "[input-mixer][defaults]")
{
    using namespace sirius;

    InputMixer mixer;

    InputDescriptor desc {
        TapeId (1), InputKind::Audio, std::string ("Guitar"), std::optional<int> (0)
    };
    mixer.registerInput (InputId (0), desc);
    mixer.setInputDefaults (InputId (0),
                            ChannelDefaults { TapeMode::CommitToTape, true });

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    // No explicit setChannelTapeMode — should inherit CommitToTape.

    // Observe via a TapeWriter: a buffer pushed through processBuffer
    // must produce a partial file iff the channel inherited CommitToTape.
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("sirius-defaults-"
                                      + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDir.createDirectory();

    TapeWriter writer (std::filesystem::path (tempDir.getFullPathName().toStdString()),
                       std::chrono::milliseconds (1), 16);
    OverloadProtection overload;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);

    std::array<std::byte, 8> buffer {};
    mixer.processBuffer (ch, buffer.data(), buffer.size());
    const auto partialPath = writer.flushChannel (ch);
    juce::File partial (juce::String (partialPath.string()));
    CHECK (partial.existsAsFile());

    tempDir.deleteRecursively();
}
