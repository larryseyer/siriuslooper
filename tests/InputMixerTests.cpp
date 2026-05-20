// Tests for sirius::InputMixer — real-body coverage added in M3 Session 2.
// The M2 stubs have been replaced with real implementations; these tests
// verify channel registration, tape-bearing buffer dispatch, and overload
// reporting on queue-full. M5 Session 1 adds [audio-dsp] coverage —
// InputMixer now invokes ChannelStrip<Audio>::process before tape writes
// (per V7 alignment plan amendment §3).
//
// Note: TapeWriter takes std::chrono::milliseconds for the flush interval;
// the caller converts from CapabilityTier before constructing.
#include "sirius/Channel.h"
#include "sirius/ChannelStrip.h"
#include "sirius/InputMixer.h"
#include "sirius/NotificationBus.h"
#include "sirius/OverloadProtection.h"
#include "sirius/TapeStore.h"
#include "sirius/TapeWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <cstring>
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

// M6 Session 2 — queue-full now ALSO posts a Warning/CpuPressure notification
// onto the engine→UI truthfulness channel. Mirrors the [input-mixer][overload]
// case above, but binds a NotificationBus and asserts the drain side sees the
// post (instead of asserting on the OverloadProtection load fraction).
TEST_CASE ("InputMixer::processBuffer posts a CpuPressure notification on queue-full",
           "[input-mixer][notification]")
{
    using sirius::Category;
    using sirius::InputId;
    using sirius::Notification;
    using sirius::NotificationBus;
    using sirius::NotificationLevel;
    using sirius::OverloadProtection;
    using sirius::SignalType;
    using sirius::TapeMode;
    using sirius::TapeWriter;

    const auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("sirius-inputmixer-notification-"
                                                + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDirJuce.createDirectory();
    const std::filesystem::path tempDir (tempDirJuce.getFullPathName().toStdString());

    // Slow flush + tiny queue keeps the queue full once we push past capacity.
    TapeWriter writer (tempDir, std::chrono::milliseconds (1000), 2);
    OverloadProtection overload;
    NotificationBus bus;

    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);
    mixer.setNotificationBus (&bus);
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);

    std::array<std::byte, 16> buffer {};
    for (int i = 0; i < 5; ++i)
        mixer.processBuffer (ch, buffer.data(), buffer.size());

    std::vector<Notification> drained;
    drained.reserve (sirius::kCategoryCount * NotificationBus::kRingCapacity);
    bus.drain (drained);

    // At least one Warning/CpuPressure notification must have been posted for
    // the 3 drops (5 pushes against a 2-slot queue). The drain may also see
    // duplicate posts; we only assert the truthfulness signal exists.
    int cpuWarnings = 0;
    for (const auto& n : drained)
        if (n.level == NotificationLevel::Warning && n.category == Category::CpuPressure)
            ++cpuWarnings;
    CHECK (cpuWarnings >= 1);

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

TEST_CASE ("InputMixer applies ChannelStrip<Audio> gain before enqueueing to TapeWriter",
           "[input-mixer][audio-dsp]")
{
    using namespace sirius;

    // Per V7 alignment plan amendment §3 / M5 Session 1: InputMixer channels
    // run their ProcessingChain on the inbound buffer before writing to tape.
    // For Audio channels, that means ChannelStrip<Audio>::process — so a
    // buffer of all-1.0f samples with strip gain=0.5 must land on tape as
    // all-0.5f samples.
    auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("sirius-audio-dsp-"
                                          + juce::String (juce::Time::getMillisecondCounterHiRes()));
    tempDirJuce.createDirectory();
    const std::filesystem::path tempDir (tempDirJuce.getFullPathName().toStdString());

    TapeWriter writer (tempDir, std::chrono::milliseconds (1), 64);
    OverloadProtection overload;
    InputMixer mixer;
    mixer.setTapeWriter (&writer);
    mixer.setOverloadProtection (&overload);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);

    // Reach the channel's strip and dial gain to 0.5.
    auto* chain = mixer.processingChainFor (ch);
    REQUIRE (chain != nullptr);
    auto* strip = dynamic_cast<ChannelStrip<SignalType::Audio>*> (chain);
    REQUIRE (strip != nullptr);
    strip->setGain (0.5f);

    // Build a buffer of 8 all-1.0f floats and pass it in as raw bytes (the
    // exact wire shape AudioCallback uses).
    constexpr std::size_t kSamples = 8;
    std::array<float, kSamples> inputFloats;
    inputFloats.fill (1.0f);
    const auto* asBytes = reinterpret_cast<const std::byte*> (inputFloats.data());
    const std::size_t byteCount = kSamples * sizeof (float);

    mixer.processBuffer (ch, asBytes, byteCount);

    // Source buffer must be unmodified — DirectLayer raw routes read the
    // same float pointers; mutating would break the raw-monitor contract.
    for (float v : inputFloats) CHECK (v == 1.0f);

    // The partial's bytes must decode to all-0.5f.
    const juce::File partial (juce::String (writer.flushChannel (ch).string()));
    REQUIRE (partial.existsAsFile());
    REQUIRE (partial.getSize() == static_cast<juce::int64> (byteCount));

    juce::MemoryBlock bytes;
    REQUIRE (partial.loadFileAsData (bytes));
    REQUIRE (bytes.getSize() == byteCount);

    std::array<float, kSamples> recorded {};
    std::memcpy (recorded.data(), bytes.getData(), byteCount);
    for (float v : recorded) CHECK (v == Catch::Approx (0.5f));

    juce::File (juce::String (tempDir.string())).deleteRecursively();
}

// =============================================================================
// [input-source] — the mixer-strip source model (whitepaper §6.1/§6.2):
// each channel sources 1 device channel (mono, dual-mono inward) or 2 (stereo
// L/R). processDeviceInputs gathers the strip's source channels into a stereo
// block and runs ChannelStrip<Audio>::process, populating the peak meters the
// Input Mixer UI reads on its timer. This is distinct from the per-device-
// channel processBuffer tape path: only channels with a source descriptor are
// processed here, and the device buffers are never mutated.
// =============================================================================

namespace
{
    // Equal-power pan gain at centre (pan = 0.5): cos(0.5 * pi/2) = 0.70710678.
    constexpr float kCentrePanGain = 0.70710678f;
}

TEST_CASE ("processDeviceInputs meters a stereo source's L and R independently",
           "[input-mixer][input-source]")
{
    using sirius::ChannelStrip;
    using sirius::InputId;
    using sirius::SignalType;

    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelInputSource (ch, /*left*/ 0, /*right*/ 1, /*stereo*/ true);

    constexpr int kSamples = 8;
    std::array<float, kSamples> left;  left.fill (0.5f);
    std::array<float, kSamples> right; right.fill (0.25f);
    const float* deviceIn[2] { left.data(), right.data() };

    mixer.processDeviceInputs (deviceIn, 2, kSamples);

    auto* strip = dynamic_cast<ChannelStrip<SignalType::Audio>*> (
        mixer.processingChainFor (ch));
    REQUIRE (strip != nullptr);
    // Default gain 1.0, pan centre → both sides scaled by kCentrePanGain.
    CHECK (strip->peakLeft()  == Catch::Approx (0.5f  * kCentrePanGain).margin (1e-5));
    CHECK (strip->peakRight() == Catch::Approx (0.25f * kCentrePanGain).margin (1e-5));

    // The device buffers must be untouched (raw-monitor contract).
    for (float v : left)  CHECK (v == Catch::Approx (0.5f));
    for (float v : right) CHECK (v == Catch::Approx (0.25f));
}

TEST_CASE ("processDeviceInputs presents a mono source dual-mono and pans it",
           "[input-mixer][input-source]")
{
    using sirius::ChannelStrip;
    using sirius::InputId;
    using sirius::SignalType;

    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId (3), SignalType::Audio);
    // Mono source on device channel 3 (right index ignored when stereo=false).
    mixer.setChannelInputSource (ch, /*left*/ 3, /*right*/ -1, /*stereo*/ false);

    constexpr int kSamples = 8;
    std::array<float, kSamples> c0 {}, c1 {}, c2 {}, c3;
    c3.fill (0.5f);
    const float* deviceIn[4] { c0.data(), c1.data(), c2.data(), c3.data() };

    auto* strip = dynamic_cast<ChannelStrip<SignalType::Audio>*> (
        mixer.processingChainFor (ch));
    REQUIRE (strip != nullptr);

    // Centre pan: the dual-mono signal lands equally on both sides.
    mixer.processDeviceInputs (deviceIn, 4, kSamples);
    CHECK (strip->peakLeft()  == Catch::Approx (0.5f * kCentrePanGain).margin (1e-5));
    CHECK (strip->peakRight() == Catch::Approx (0.5f * kCentrePanGain).margin (1e-5));

    // Hard left: full level on L, silence on R — the mono source is positioned.
    strip->setPan (0.0f);
    mixer.processDeviceInputs (deviceIn, 4, kSamples);
    CHECK (strip->peakLeft()  == Catch::Approx (0.5f).margin (1e-5));
    CHECK (strip->peakRight() == Catch::Approx (0.0f).margin (1e-5));
}

TEST_CASE ("processDeviceInputs ignores channels without a source descriptor",
           "[input-mixer][input-source]")
{
    using sirius::ChannelStrip;
    using sirius::InputId;
    using sirius::SignalType;

    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);  // no source set

    constexpr int kSamples = 8;
    std::array<float, kSamples> left; left.fill (0.9f);
    const float* deviceIn[1] { left.data() };

    mixer.processDeviceInputs (deviceIn, 1, kSamples);

    auto* strip = dynamic_cast<ChannelStrip<SignalType::Audio>*> (
        mixer.processingChainFor (ch));
    REQUIRE (strip != nullptr);
    CHECK (strip->peakLeft()  == Catch::Approx (0.0f));
    CHECK (strip->peakRight() == Catch::Approx (0.0f));
}

TEST_CASE ("processDeviceInputs safely skips a source whose device channel is out of range",
           "[input-mixer][input-source]")
{
    using sirius::ChannelStrip;
    using sirius::InputId;
    using sirius::SignalType;

    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelInputSource (ch, /*left*/ 5, /*right*/ 6, /*stereo*/ true);  // device only has 2

    constexpr int kSamples = 8;
    std::array<float, kSamples> left;  left.fill (0.5f);
    std::array<float, kSamples> right; right.fill (0.5f);
    const float* deviceIn[2] { left.data(), right.data() };

    mixer.processDeviceInputs (deviceIn, 2, kSamples);  // must not crash

    auto* strip = dynamic_cast<ChannelStrip<SignalType::Audio>*> (
        mixer.processingChainFor (ch));
    REQUIRE (strip != nullptr);
    CHECK (strip->peakLeft()  == Catch::Approx (0.0f));
    CHECK (strip->peakRight() == Catch::Approx (0.0f));
}
