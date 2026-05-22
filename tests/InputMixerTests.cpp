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
#include "sirius/ITapeSink.h"
#include "sirius/MixerGraphState.h"
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
#include <utility>

using sirius::InputMixer;

static_assert (std::is_default_constructible_v<InputMixer>,
               "InputMixer must remain default-constructible");
static_assert (std::is_destructible_v<InputMixer>);
static_assert (noexcept (std::declval<sirius::InputMixer&>().renderInputGraph (
                   nullptr, 0, nullptr, 0, 0)),
               "InputMixer::renderInputGraph must be noexcept (RT-safety contract §6)");

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

    // Test-only: deliverTapeBlock allocates (vector copies). NOT a production
    // ITapeSink implementation — a real sink must be allocation-free.
    struct RecordingTapeSink : sirius::ITapeSink
    {
        struct Block { std::int64_t tapeId; std::vector<float> left, right; };
        std::vector<Block> blocks;

        void deliverTapeBlock (sirius::TapeId tape, const float* l, const float* r,
                               int n) noexcept override
        {
            blocks.push_back ({ tape.value(),
                                std::vector<float> (l, l + n),
                                std::vector<float> (r, r + n) });
        }

        const Block* find (std::int64_t tapeId) const
        {
            for (const auto& b : blocks) if (b.tapeId == tapeId) return &b;
            return nullptr;
        }
    };

    sirius::ChannelId addStereoChannel (sirius::InputMixer& mixer, int leftDev, int rightDev)
    {
        using sirius::InputId; using sirius::SignalType;
        const auto ch = mixer.addChannel (InputId { 1 }, SignalType::Audio);
        mixer.setChannelTapeMode (ch, sirius::TapeMode::CommitToTape);
        mixer.setChannelInputSource (ch, leftDev, rightDev, true);
        return ch;
    }
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

// =============================================================================
// [input-routing] — Phase 3 Task 2: MixerGraph + bus/FX-return registry
// =============================================================================

TEST_CASE ("InputMixer constructs with Tape+HardwareOutput terminals and default RVB/DLY returns",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    CHECK (mixer.busCount() == 2);
    CHECK (mixer.busKindAt (0) == sirius::BusKind::FxReturn);
    CHECK (mixer.busKindAt (1) == sirius::BusKind::FxReturn);
}

TEST_CASE ("InputMixer addBus registers a graph node defaulting its main-out to the tape terminal",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    const auto bus = mixer.addBus (sirius::BusConfig { 2, "Drums", sirius::BusKind::Bus });
    CHECK (bus.value() != 0);
    CHECK (mixer.busCount() == 3); // RVB, DLY, Drums
    CHECK (mixer.busMainOutIsTape (bus));
}

TEST_CASE ("InputMixer addChannel registers a Channel graph node; removeChannel drops it",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    const auto ch = mixer.addChannel (sirius::InputId { 0 }, sirius::SignalType::Audio);
    CHECK (mixer.channelIsRegisteredInGraph (ch));
    mixer.removeChannel (ch);
    CHECK_FALSE (mixer.channelIsRegisteredInGraph (ch));
}

TEST_CASE ("InputMixer default RVB/DLY returns monitor the hardware output, not tape",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    CHECK (mixer.busMainOut (mixer.busIdAt (0)) == sirius::InputMixer::MainOutDest::HardwareOutput);
    CHECK (mixer.busMainOut (mixer.busIdAt (1)) == sirius::InputMixer::MainOutDest::HardwareOutput);
}

TEST_CASE ("InputMixer routes a channel main-out to a bus, the tape, or a hardware output",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    const auto ch  = mixer.addChannel (sirius::InputId { 0 }, sirius::SignalType::Audio);
    const auto bus = mixer.addBus (sirius::BusConfig { 2, "Drums", sirius::BusKind::Bus });

    SECTION ("default is the tape terminal")
    {
        CHECK (mixer.channelMainOut (ch) == sirius::InputMixer::MainOutDest::Tape);
    }
    SECTION ("to a bus")
    {
        CHECK (mixer.setChannelMainOutToBus (ch, bus));
        CHECK (mixer.channelMainOut (ch) == sirius::InputMixer::MainOutDest::Bus);
    }
    SECTION ("to the hardware output (RME direct-out monitoring)")
    {
        CHECK (mixer.setChannelMainOutToHardwareOutput (ch));
        CHECK (mixer.channelMainOut (ch) == sirius::InputMixer::MainOutDest::HardwareOutput);
    }
}

TEST_CASE ("InputMixer rejects a channel send to a non-FX-return and accepts one to an FX return",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    const auto ch  = mixer.addChannel (sirius::InputId { 0 }, sirius::SignalType::Audio);
    const auto bus = mixer.addBus (sirius::BusConfig { 2, "Drums", sirius::BusKind::Bus });
    const auto rvb = mixer.addFxReturn ("RVB2");

    CHECK_FALSE (mixer.setChannelSend (ch, bus, 0.5f)); // a Bus is not an FX return
    CHECK (mixer.setChannelSend (ch, rvb, 0.5f));
    CHECK (mixer.channelSendLevel (ch, rvb) == 0.5f);
}

TEST_CASE ("InputMixer rejects a bus main-out assignment that would create a cycle",
           "[input-routing]")
{
    sirius::InputMixer mixer;
    const auto a = mixer.addBus (sirius::BusConfig { 2, "A", sirius::BusKind::Bus });
    const auto b = mixer.addBus (sirius::BusConfig { 2, "B", sirius::BusKind::Bus });
    REQUIRE (mixer.setBusMainOutToBus (a, b)); // A -> B
    CHECK_FALSE (mixer.setBusMainOutToBus (b, a)); // closing the loop is refused
}

// =============================================================================
// [input-routing][render] — Phase 3 Task 4: renderInputGraph channel delivery
// =============================================================================

TEST_CASE ("renderInputGraph: default-graph tape-routed channel delivers its processed block to the tape sink",
           "[input-routing][render]")
{
    using sirius::InputMixer; using sirius::InputId; using sirius::SignalType;
    using sirius::TapeMode;

    RecordingTapeSink sink;
    InputMixer mixer;
    mixer.setTapeSink (&sink);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, /*stereo*/ true);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);
    // default main-out = Tape terminal — no routing call needed.

    constexpr int n = 64;
    std::vector<float> left (n, 0.5f), right (n, 0.25f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { nullptr, nullptr };

    mixer.renderInputGraph (deviceIn, 2, directOut, 0, n);

    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);
    CHECK (b->left.size() == (std::size_t) n);
}

TEST_CASE ("renderInputGraph: a channel routed to the hardware output sums into direct-out, not tape",
           "[input-routing][render]")
{
    using sirius::InputMixer; using sirius::InputId; using sirius::SignalType;
    using sirius::TapeMode;

    RecordingTapeSink sink;
    InputMixer mixer;
    mixer.setTapeSink (&sink);

    const auto ch = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    mixer.setChannelTapeMode (ch, TapeMode::CommitToTape);
    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch)); // direct-out, NOT tape

    constexpr int n = 32;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, n);

    // Assert ROUTING, not exact DSP (the strip applies an equal-power pan law, so
    // a centred unity strip is ~0.707, not 1.0): signal reached direct-out, tape did not.
    CHECK (outL[0] != 0.0f);
    CHECK (outR[0] != 0.0f);
    CHECK (sink.blocks.empty()); // routed to hardware output, not tape
}


TEST_CASE ("renderInputGraph: a channel send reaches an FX return, which delivers to direct-out",
           "[input-routing][render]")
{
    using sirius::InputMixer; using sirius::InputId; using sirius::SignalType;

    InputMixer mixer;
    const auto ch  = mixer.addChannel (InputId (0), SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);
    const auto rvb = mixer.addFxReturn ("RVB2");
    // Isolate the SEND path: dry main-out -> tape (off direct-out); the only path to
    // direct-out is send -> FX return -> hardware output. Non-zero direct-out proves it.
    REQUIRE (mixer.setChannelMainOutToTape (ch));
    REQUIRE (mixer.setChannelSend (ch, rvb, 1.0f));
    REQUIRE (mixer.setBusMainOutToHardwareOutput (rvb));

    constexpr int n = 16;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, n);

    // FX return has no DSP yet (empty chain = pass-through); direct-out is non-zero
    // ONLY via send -> FX-return -> hardware-output.
    CHECK (outL[0] != 0.0f);
    CHECK (outR[0] != 0.0f);
}

TEST_CASE ("InputMixer exportGraphState reflects buses, routing, sends, inserts",
           "[input-mixer][persistence]")
{
    // The InputMixer ctor pre-builds two FX returns (RVB busId 1, DLY busId 2),
    // so the bus vector is [RVB, DLY, Drums, Reverb] and the snapshot reflects
    // them all — exportGraphState mirrors the live mixer, ctor nodes included.
    sirius::InputMixer mixer;
    const sirius::InputDescriptor desc {
        sirius::TapeId (1), sirius::InputKind::Audio, std::string ("In 1"),
        std::optional<int> (0)
    };
    mixer.registerInput (sirius::InputId (1), desc);

    const auto drums = mixer.addBus (sirius::BusConfig { 2, "Drums", sirius::BusKind::Bus });
    const auto reverb = mixer.addFxReturn ("Reverb");

    sirius::EffectChainEntry comp; comp.displayName = "comp";
    mixer.setBusEffectChain (drums, sirius::EffectChain{}.withAppended (comp));

    const auto ch = mixer.addChannel (sirius::InputId (1), sirius::SignalType::Audio);
    mixer.setChannelInputSource (ch, 2, 3, true);
    mixer.setChannelTapeMode (ch, sirius::TapeMode::CommitToTape);
    mixer.setChannelMainOutToBus (ch, drums);
    mixer.setChannelSend (ch, reverb, 0.5f);

    auto* chain = mixer.processingChainFor (ch);
    REQUIRE (chain != nullptr);
    auto* strip = static_cast<sirius::ChannelStrip<sirius::SignalType::Audio>*> (chain);
    sirius::EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (sirius::EffectChain{}.withAppended (eq));

    const auto state = mixer.exportGraphState();

    REQUIRE (state.buses.size() == 4);
    CHECK (state.buses[0].kind == sirius::MixerBusKind::FxReturn); // RVB
    CHECK (state.buses[1].kind == sirius::MixerBusKind::FxReturn); // DLY

    const auto& drumsState = state.buses[2];
    CHECK (drumsState.busId == drums.value());
    CHECK (drumsState.name == "Drums");
    CHECK (drumsState.kind == sirius::MixerBusKind::Bus);
    CHECK (drumsState.inserts.entries().size() == 1);

    const auto& reverbState = state.buses[3];
    CHECK (reverbState.busId == reverb.value());
    CHECK (reverbState.kind == sirius::MixerBusKind::FxReturn);

    REQUIRE (state.channels.size() == 1);
    const auto& c = state.channels[0];
    CHECK (c.channelId == ch.value());
    CHECK (c.inputSourceId == 1);
    CHECK (c.source == sirius::MixerChannelSource { 2, 3, true });
    CHECK (c.tapeMode == sirius::TapeMode::CommitToTape);
    CHECK (c.mainOut.kind == sirius::MixerMainOut::Kind::Bus);
    CHECK (c.mainOut.busId == drums.value());
    REQUIRE (c.sends.size() == 1);
    CHECK (c.sends[0].busId == reverb.value());
    CHECK (c.sends[0].level == 0.5f);
    CHECK (c.inserts.entries().size() == 1);
}

TEST_CASE ("InputMixer importGraphState round-trips an exported graph", "[input-mixer][persistence]")
{
    sirius::InputMixer source;
    const sirius::InputDescriptor desc {
        sirius::TapeId (1), sirius::InputKind::Audio, std::string ("In 1"),
        std::optional<int> (0)
    };
    source.registerInput (sirius::InputId (1), desc);
    const auto drums  = source.addBus (sirius::BusConfig { 2, "Drums", sirius::BusKind::Bus });
    const auto reverb = source.addFxReturn ("Reverb");
    sirius::EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (drums, sirius::EffectChain{}.withAppended (comp));
    // Exercise the "apply chain to an EXISTING ctor bus" import path (busId 1 = RVB).
    sirius::EffectChainEntry rvbVerb; rvbVerb.displayName = "rvb-plate";
    source.setBusEffectChain (sirius::BusId (1), sirius::EffectChain{}.withAppended (rvbVerb));
    const auto ch = source.addChannel (sirius::InputId (1), sirius::SignalType::Audio);
    source.setChannelInputSource (ch, 2, 3, true);
    source.setChannelTapeMode (ch, sirius::TapeMode::CommitToTape);
    source.setChannelMainOutToBus (ch, drums);
    source.setChannelSend (ch, reverb, 0.5f);

    const auto exported = source.exportGraphState();

    sirius::InputMixer loaded;
    loaded.importGraphState (exported);

    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("InputMixer importGraphState of an empty snapshot keeps only the ctor FX returns", "[input-mixer][persistence]")
{
    sirius::InputMixer mixer;
    mixer.importGraphState (sirius::InputMixerGraphState{});

    // The ctor seeds RVB (busId 1) + DLY (busId 2); an empty import adds no user
    // buses and must not duplicate or rewind them — the pre-graph "loads clean" default.
    CHECK (mixer.busCount() == 2);

    const sirius::InputDescriptor desc {
        sirius::TapeId (1), sirius::InputKind::Audio, std::string ("In 1"),
        std::optional<int> (0)
    };
    mixer.registerInput (sirius::InputId (1), desc);
    const auto ch = mixer.addChannel (sirius::InputId (1), sirius::SignalType::Audio);
    CHECK (mixer.channelMainOut (ch) == sirius::InputMixer::MainOutDest::Tape);
}

TEST_CASE ("InputMixer::busForId returns the bus for a known id, nullptr otherwise",
           "[input-mixer][bus-access]")
{
    sirius::InputMixer mixer;
    const sirius::BusId id = mixer.addBus (sirius::BusConfig { 2, "Drum Bus", sirius::BusKind::Bus });

    sirius::Bus* bus = mixer.busForId (id);
    REQUIRE (bus != nullptr);
    REQUIRE (bus->id() == id);
    REQUIRE (bus->config().name == "Drum Bus");

    // Round-trip a control through the accessor (what the UI does).
    bus->setGain (0.3f);
    REQUIRE (mixer.busForId (id)->gain() == Catch::Approx (0.3f));

    REQUIRE (mixer.busForId (sirius::BusId { 9999 }) == nullptr);
}

TEST_CASE ("InputMixer: a freshly constructed mixer has exactly the primary tape (TapeId 1)",
           "[input-mixer][multi-tape]")
{
    using sirius::InputMixer; using sirius::TapeId;
    InputMixer mixer;
    CHECK (mixer.tapeCount() == 1);
    CHECK (mixer.hasTape (TapeId { 1 }));
    CHECK_FALSE (mixer.hasTape (TapeId { 2 }));
}

TEST_CASE ("InputMixer: addTape registers a routable terminal; removeTape unregisters it; primary is permanent",
           "[input-mixer][multi-tape]")
{
    using sirius::InputMixer; using sirius::TapeId;
    InputMixer mixer;

    CHECK (mixer.addTape (TapeId { 2 }));
    CHECK (mixer.tapeCount() == 2);
    CHECK (mixer.hasTape (TapeId { 2 }));
    CHECK_FALSE (mixer.addTape (TapeId { 2 }));   // duplicate refused

    CHECK (mixer.removeTape (TapeId { 2 }));
    CHECK_FALSE (mixer.hasTape (TapeId { 2 }));
    CHECK_FALSE (mixer.removeTape (TapeId { 99 })); // unknown refused
    CHECK_FALSE (mixer.removeTape (TapeId { 1 }));  // primary refused
    CHECK (mixer.tapeCount() == 1);
}

TEST_CASE ("InputMixer: a channel routes to a chosen tape; the no-arg overload targets the primary",
           "[input-mixer][multi-tape]")
{
    using sirius::InputMixer; using sirius::InputId; using sirius::SignalType; using sirius::TapeId;
    InputMixer mixer;
    REQUIRE (mixer.addTape (TapeId { 2 }));
    const auto ch = mixer.addChannel (InputId { 1 }, SignalType::Audio);

    REQUIRE (mixer.setChannelMainOutToTape (ch, TapeId { 2 }));
    CHECK (mixer.channelMainOut (ch) == InputMixer::MainOutDest::Tape);
    CHECK (mixer.channelMainOutIsTape (ch, TapeId { 2 }));
    CHECK_FALSE (mixer.channelMainOutIsTape (ch, TapeId { 1 }));

    REQUIRE (mixer.setChannelMainOutToTape (ch));   // no-arg → primary
    CHECK (mixer.channelMainOutIsTape (ch, TapeId { 1 }));

    CHECK_FALSE (mixer.setChannelMainOutToTape (ch, TapeId { 99 })); // unknown tape refused
}

TEST_CASE ("InputMixer: a bus routes to a chosen tape via setBusMainOutToTape(BusId, TapeId)",
           "[input-mixer][multi-tape]")
{
    using sirius::InputMixer; using sirius::BusId; using sirius::BusConfig; using sirius::TapeId;
    InputMixer mixer;
    REQUIRE (mixer.addTape (TapeId { 2 }));
    const auto bus = mixer.addBus (BusConfig { 2, "Sub", sirius::BusKind::Bus });

    REQUIRE (mixer.setBusMainOutToTape (bus, TapeId { 2 }));
    CHECK (mixer.busMainOut (bus) == InputMixer::MainOutDest::Tape);
    CHECK (mixer.busMainOutIsTape (bus));                 // routed to *a* tape
    CHECK (mixer.busMainOutIsTape (bus, TapeId { 2 }));
    CHECK_FALSE (mixer.busMainOutIsTape (bus, TapeId { 1 }));
}

// =============================================================================
// [input-mixer][multi-tape][render] — tape subsystem slice 2: per-tape summing
// and ITapeSink delivery in renderInputGraph
// =============================================================================

TEST_CASE ("renderInputGraph: a tape-routed channel is delivered to that tape via the sink",
           "[input-mixer][multi-tape][render]")
{
    using sirius::InputMixer; using sirius::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto ch = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (ch)); // primary tape (1)

    constexpr int n = 8;
    std::vector<float> l (n, 0.5f), r (n, 0.25f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);
    REQUIRE (b->left.size() == (std::size_t) n);
    // ChannelStrip applies equal-power pan gain at default centre (kCentrePanGain).
    CHECK (b->left[0]  == Catch::Approx (0.5f  * kCentrePanGain).margin (1e-5f));
    CHECK (b->right[0] == Catch::Approx (0.25f * kCentrePanGain).margin (1e-5f));
    CHECK (sink.blocks.size() == 1);
}

TEST_CASE ("renderInputGraph: two channels on one tape SUM into a single delivery",
           "[input-mixer][multi-tape][render]")
{
    using sirius::InputMixer;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto chA = addStereoChannel (mixer, 0, 1);
    const auto chB = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (chA));
    REQUIRE (mixer.setChannelMainOutToTape (chB));

    constexpr int n = 4;
    std::vector<float> l (n, 0.3f), r (n, 0.1f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    REQUIRE (sink.blocks.size() == 1);
    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);
    // Two channels each scaled by kCentrePanGain, then summed.
    CHECK (b->left[0]  == Catch::Approx (0.3f * 2.0f * kCentrePanGain).margin (1e-5f));
    CHECK (b->right[0] == Catch::Approx (0.1f * 2.0f * kCentrePanGain).margin (1e-5f));
}

TEST_CASE ("renderInputGraph: channels on distinct tapes record in parallel",
           "[input-mixer][multi-tape][render]")
{
    using sirius::InputMixer; using sirius::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);
    REQUIRE (mixer.addTape (TapeId { 2 }));

    const auto chA = addStereoChannel (mixer, 0, 1);
    const auto chB = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (chA, TapeId { 1 }));
    REQUIRE (mixer.setChannelMainOutToTape (chB, TapeId { 2 }));

    constexpr int n = 4;
    std::vector<float> l (n, 0.4f), r (n, 0.4f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    CHECK (sink.blocks.size() == 2);
    REQUIRE (sink.find (1) != nullptr);
    REQUIRE (sink.find (2) != nullptr);
    // Each channel scaled by kCentrePanGain before delivery.
    CHECK (sink.find (1)->left[0] == Catch::Approx (0.4f * kCentrePanGain).margin (1e-5f));
    CHECK (sink.find (2)->left[0] == Catch::Approx (0.4f * kCentrePanGain).margin (1e-5f));
}

TEST_CASE ("renderInputGraph: channel -> bus -> tape delivers the bus output to the chosen tape",
           "[input-mixer][multi-tape][render]")
{
    using sirius::InputMixer; using sirius::BusId; using sirius::BusConfig; using sirius::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto bus = mixer.addBus (BusConfig { 2, "Sub", sirius::BusKind::Bus });
    const auto ch  = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToBus (ch, bus));
    REQUIRE (mixer.setBusMainOutToTape (bus));

    constexpr int n = 4;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    const auto* b = sink.find (1);
    REQUIRE (b != nullptr);
    // Channel goes through ChannelStrip (kCentrePanGain at default centre pan),
    // accumulates into bus, then bus.process at unity gain delivers to tape.
    CHECK (b->left[0] == Catch::Approx (0.5f * kCentrePanGain).margin (1e-5f));
}

TEST_CASE ("renderInputGraph: with no sink bound, tape-routed signal is dropped without crashing",
           "[input-mixer][multi-tape][render]")
{
    using sirius::InputMixer;
    InputMixer mixer; // no setTapeSink
    const auto ch = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (ch));

    constexpr int n = 4;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);
    SUCCEED();
}

TEST_CASE ("renderInputGraph: removing a tape a channel was routed to falls the channel back to the primary",
           "[input-mixer][multi-tape][render]")
{
    using sirius::InputMixer; using sirius::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);
    REQUIRE (mixer.addTape (TapeId { 2 }));

    const auto ch = addStereoChannel (mixer, 0, 1);
    REQUIRE (mixer.setChannelMainOutToTape (ch, TapeId { 2 }));

    // Remove the tape the channel targets. The graph reassigns the orphaned
    // main-out to the primary terminal; the InputMixer slot bookkeeping must
    // stay consistent so the next render delivers to the primary tape (1).
    REQUIRE (mixer.removeTape (TapeId { 2 }));
    CHECK (mixer.channelMainOutIsTape (ch, TapeId { 1 }));

    constexpr int n = 4;
    std::vector<float> l (n, 0.5f), r (n, 0.5f);
    const float* deviceIn[2] { l.data(), r.data() };
    mixer.renderInputGraph (deviceIn, 2, nullptr, 0, n);

    REQUIRE (sink.find (2) == nullptr);  // the removed tape receives nothing
    REQUIRE (sink.find (1) != nullptr);  // signal fell back to the primary
}

TEST_CASE ("exportGraphState round-trips a non-primary tape route", "[input-mixer][tape]")
{
    sirius::InputMixer mixer;
    const auto ch = mixer.addChannel (sirius::InputId (0), sirius::SignalType::Audio);
    REQUIRE (mixer.addTape (sirius::TapeId { 2 }));
    REQUIRE (mixer.setChannelMainOutToTape (ch, sirius::TapeId { 2 }));

    // Must NOT trip the mainOutSnapshot jassertfalse, and must record tape 2.
    const auto state = mixer.exportGraphState();
    REQUIRE (state.channels.size() == 1);
    const auto& mo = state.channels[0].mainOut;
    CHECK (mo.kind     == sirius::MixerMainOut::Kind::Terminal);
    CHECK (mo.terminal == sirius::MixerTerminalKind::Tape);
    CHECK (mo.tapeId   == 2);

    // Re-import into a fresh mixer (with tape 2 present) restores the route.
    sirius::InputMixer restored;
    REQUIRE (restored.addTape (sirius::TapeId { 2 }));
    restored.importGraphState (state);
    CHECK (restored.channelMainOutIsTape (sirius::ChannelId (state.channels[0].channelId),
                                          sirius::TapeId { 2 }));
}

TEST_CASE ("InputMixer reports which bus a node's main-out targets", "[input-mixer]")
{
    sirius::InputMixer mixer;
    const auto a  = mixer.addBus (sirius::BusConfig { 2, "A", sirius::BusKind::Bus });
    const auto b  = mixer.addBus (sirius::BusConfig { 2, "B", sirius::BusKind::Bus });
    const auto ch = mixer.addChannel (sirius::InputId (0), sirius::SignalType::Audio);

    REQUIRE (mixer.setChannelMainOutToBus (ch, a));
    REQUIRE (mixer.channelMainOutBus (ch).value() == a.value());
    REQUIRE (mixer.setBusMainOutToBus (a, b));
    REQUIRE (mixer.busMainOutBus (a).value() == b.value());

    REQUIRE (mixer.setChannelMainOutToHardwareOutput (ch));
    REQUIRE (mixer.channelMainOutBus (ch).value() == 0);
    REQUIRE (mixer.setBusMainOutToTape (b));
    REQUIRE (mixer.busMainOutBus (b).value() == 0);
}

TEST_CASE ("InputMixer flags bus main-out routes that would cycle", "[input-mixer]")
{
    sirius::InputMixer mixer;
    const auto a = mixer.addBus (sirius::BusConfig { 2, "A", sirius::BusKind::Bus });
    const auto b = mixer.addBus (sirius::BusConfig { 2, "B", sirius::BusKind::Bus });
    REQUIRE (mixer.setBusMainOutToBus (a, b));           // a -> b
    REQUIRE (mixer.busMainOutToBusWouldCycle (b, a));    // b -> a would close the loop
    REQUIRE_FALSE (mixer.busMainOutToBusWouldCycle (a, b));
}
