// Tests for ida::InputMixer — real-body coverage added in M3 Session 2.
// The M2 stubs have been replaced with real implementations; these tests
// verify channel registration, tape-bearing buffer dispatch, and overload
// reporting on queue-full. M5 Session 1 adds [audio-dsp] coverage —
// InputMixer now invokes ChannelStrip<Audio>::process before tape writes
// (per V7 alignment plan amendment §3).
//
// Note: TapeWriter takes std::chrono::milliseconds for the flush interval;
// the caller converts from CapabilityTier before constructing.
#include "ida/Channel.h"
#include "ida/ChannelStrip.h"
#include "ida/InputMixer.h"
#include "ida/ITapeSink.h"
#include "ida/MixerGraphState.h"
#include "ida/NotificationBus.h"
#include "ida/OverloadProtection.h"
#include "ida/TapeStore.h"
#include "ida/TapeWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <type_traits>
#include <utility>

using ida::InputMixer;

static_assert (std::is_default_constructible_v<InputMixer>,
               "InputMixer must remain default-constructible");
static_assert (std::is_destructible_v<InputMixer>);
static_assert (noexcept (std::declval<ida::InputMixer&>().renderInputGraph (
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
    using ida::ChannelId;
    using ida::InputId;
    using ida::OverloadProtection;
    using ida::SignalType;
    using ida::TapeMode;
    using ida::TapeWriter;

    const auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("ida-inputmixer-process-"
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
    using ida::InputId;
    using ida::OverloadProtection;
    using ida::SignalType;
    using ida::TapeMode;
    using ida::TapeWriter;

    const auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("ida-inputmixer-overload-"
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
    using ida::Category;
    using ida::InputId;
    using ida::Notification;
    using ida::NotificationBus;
    using ida::NotificationLevel;
    using ida::OverloadProtection;
    using ida::SignalType;
    using ida::TapeMode;
    using ida::TapeWriter;

    const auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("ida-inputmixer-notification-"
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
    drained.reserve (ida::kCategoryCount * NotificationBus::kRingCapacity);
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
    using namespace ida;
    using ida::persistence::TapeStore;

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ida-finalize-"
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
    using namespace ida;
    using ida::persistence::TapeStore;

    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("ida-nondestructive-"
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
    using namespace ida;

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
                       .getChildFile ("ida-defaults-"
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
    using namespace ida;

    // Per V7 alignment plan amendment §3 / M5 Session 1: InputMixer channels
    // run their ProcessingChain on the inbound buffer before writing to tape.
    // For Audio channels, that means ChannelStrip<Audio>::process — so a
    // buffer of all-1.0f samples with strip gain=0.5 must land on tape as
    // all-0.5f samples.
    auto tempDirJuce = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("ida-audio-dsp-"
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

    // Source buffer must be unmodified — the raw-input contract holds.
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
    struct RecordingTapeSink : ida::ITapeSink
    {
        struct Block { std::int64_t tapeId; std::vector<float> left, right; };
        std::vector<Block> blocks;

        void deliverTapeBlock (ida::TapeId tape, const float* l, const float* r,
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

    ida::ChannelId addStereoChannel (ida::InputMixer& mixer, int leftDev, int rightDev)
    {
        using ida::InputId; using ida::SignalType;
        const auto ch = mixer.addChannel (InputId { 1 }, SignalType::Audio);
        mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
        mixer.setChannelInputSource (ch, leftDev, rightDev, true);
        return ch;
    }
}

TEST_CASE ("processDeviceInputs meters a stereo source's L and R independently",
           "[input-mixer][input-source]")
{
    using ida::ChannelStrip;
    using ida::InputId;
    using ida::SignalType;

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
    using ida::ChannelStrip;
    using ida::InputId;
    using ida::SignalType;

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
    using ida::ChannelStrip;
    using ida::InputId;
    using ida::SignalType;

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
    using ida::ChannelStrip;
    using ida::InputId;
    using ida::SignalType;

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

TEST_CASE ("InputMixer constructs with Tape+HardwareOutput terminals and zero buses",
           "[input-routing]")
{
    ida::InputMixer mixer;
    CHECK (mixer.busCount() == 0);
    CHECK (mixer.tapeCount() == 1);                          // primary tape terminal
    CHECK (mixer.hasTape (ida::TapeId (1)));              // permanent default
}

TEST_CASE ("InputMixer addBus registers a graph node defaulting its main-out to the tape terminal",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    CHECK (bus.value() != 0);
    CHECK (mixer.busCount() == 1); // Drums
    CHECK (mixer.busMainOutIsTape (bus));
}

TEST_CASE ("InputMixer addChannel registers a Channel graph node; removeChannel drops it",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    CHECK (mixer.channelIsRegisteredInGraph (ch));
    mixer.removeChannel (ch);
    CHECK_FALSE (mixer.channelIsRegisteredInGraph (ch));
}

TEST_CASE ("InputMixer routes a channel main-out to a bus, the tape, or a hardware output",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto ch  = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });

    SECTION ("default is the tape terminal")
    {
        CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Tape);
    }
    SECTION ("to a bus")
    {
        CHECK (mixer.setChannelMainOutToBus (ch, bus));
        CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Bus);
    }
    SECTION ("to the hardware output (RME direct-out monitoring)")
    {
        CHECK (mixer.setChannelMainOutToHardwareOutput (ch));
        CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::HardwareOutput);
    }
}

TEST_CASE ("InputMixer rejects a channel send to a non-FX-return and accepts one to an FX return",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto ch  = mixer.addChannel (ida::InputId { 0 }, ida::SignalType::Audio);
    const auto bus = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto rvb = mixer.addFxReturn ("RVB2");

    CHECK_FALSE (mixer.setChannelSend (ch, bus, 0.5f)); // a Bus is not an FX return
    CHECK (mixer.setChannelSend (ch, rvb, 0.5f));
    CHECK (mixer.channelSendLevel (ch, rvb) == 0.5f);
}

TEST_CASE ("InputMixer rejects a bus main-out assignment that would create a cycle",
           "[input-routing]")
{
    ida::InputMixer mixer;
    const auto a = mixer.addBus (ida::BusConfig { 2, "A", ida::BusKind::Bus });
    const auto b = mixer.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    REQUIRE (mixer.setBusMainOutToBus (a, b)); // A -> B
    CHECK_FALSE (mixer.setBusMainOutToBus (b, a)); // closing the loop is refused
}

// =============================================================================
// [input-routing][render] — Phase 3 Task 4: renderInputGraph channel delivery
// =============================================================================

TEST_CASE ("renderInputGraph: default-graph tape-routed channel delivers its processed block to the tape sink",
           "[input-routing][render]")
{
    using ida::InputMixer; using ida::InputId; using ida::SignalType;
    using ida::TapeMode;

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
    using ida::InputMixer; using ida::InputId; using ida::SignalType;
    using ida::TapeMode;

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
    using ida::InputMixer; using ida::InputId; using ida::SignalType;

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
    // A freshly-constructed mixer has zero buses (minimal-defaults rule), so the
    // bus vector after these adds is [Drums, Reverb] and exportGraphState reflects
    // exactly what the test wired up — no ctor-seeded nodes intrude.
    ida::InputMixer mixer;
    const ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio, std::string ("In 1"),
        std::optional<int> (0)
    };
    mixer.registerInput (ida::InputId (1), desc);

    const auto drums = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto reverb = mixer.addFxReturn ("Reverb");

    ida::EffectChainEntry comp; comp.displayName = "comp";
    mixer.setBusEffectChain (drums, ida::EffectChain{}.withAppended (comp));

    const auto ch = mixer.addChannel (ida::InputId (1), ida::SignalType::Audio);
    mixer.setChannelInputSource (ch, 2, 3, true);
    mixer.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    mixer.setChannelMainOutToBus (ch, drums);
    mixer.setChannelSend (ch, reverb, 0.5f);

    auto* chain = mixer.processingChainFor (ch);
    REQUIRE (chain != nullptr);
    auto* strip = static_cast<ida::ChannelStrip<ida::SignalType::Audio>*> (chain);
    ida::EffectChainEntry eq; eq.displayName = "eq";
    strip->setEffectChain (ida::EffectChain{}.withAppended (eq));

    const auto state = mixer.exportGraphState();

    REQUIRE (state.buses.size() == 2);

    const auto& drumsState = state.buses[0];
    CHECK (drumsState.busId == drums.value());
    CHECK (drumsState.name == "Drums");
    CHECK (drumsState.kind == ida::MixerBusKind::Bus);
    CHECK (drumsState.inserts.entries().size() == 1);

    const auto& reverbState = state.buses[1];
    CHECK (reverbState.busId == reverb.value());
    CHECK (reverbState.kind == ida::MixerBusKind::FxReturn);

    REQUIRE (state.channels.size() == 1);
    const auto& c = state.channels[0];
    CHECK (c.channelId == ch.value());
    CHECK (c.inputSourceId == 1);
    CHECK (c.source == ida::MixerChannelSource { 2, 3, true });
    CHECK (c.tapeMode == ida::TapeMode::CommitToTape);
    CHECK (c.mainOut.kind == ida::MixerMainOut::Kind::Bus);
    CHECK (c.mainOut.busId == drums.value());
    REQUIRE (c.sends.size() == 1);
    CHECK (c.sends[0].busId == reverb.value());
    CHECK (c.sends[0].level == 0.5f);
    CHECK (c.inserts.entries().size() == 1);
}

TEST_CASE ("InputMixer importGraphState round-trips an exported graph", "[input-mixer][persistence]")
{
    ida::InputMixer source;
    const ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio, std::string ("In 1"),
        std::optional<int> (0)
    };
    source.registerInput (ida::InputId (1), desc);
    const auto drums  = source.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto reverb = source.addFxReturn ("Reverb");
    ida::EffectChainEntry comp; comp.displayName = "comp";
    source.setBusEffectChain (drums, ida::EffectChain{}.withAppended (comp));
    const auto ch = source.addChannel (ida::InputId (1), ida::SignalType::Audio);
    source.setChannelInputSource (ch, 2, 3, true);
    source.setChannelTapeMode (ch, ida::TapeMode::CommitToTape);
    source.setChannelMainOutToBus (ch, drums);
    source.setChannelSend (ch, reverb, 0.5f);

    const auto exported = source.exportGraphState();

    ida::InputMixer loaded;
    loaded.importGraphState (exported);

    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("InputMixer importGraphState of an empty snapshot yields an empty mixer", "[input-mixer][persistence]")
{
    ida::InputMixer mixer;
    mixer.importGraphState (ida::InputMixerGraphState{});

    // Minimal-defaults rule: a fresh mixer has zero buses, and an empty import
    // adds none — the mixer stays in the pristine ctor state and a fresh add
    // works normally (channel default-routes to the tape terminal).
    CHECK (mixer.busCount() == 0);

    const ida::InputDescriptor desc {
        ida::TapeId (1), ida::InputKind::Audio, std::string ("In 1"),
        std::optional<int> (0)
    };
    mixer.registerInput (ida::InputId (1), desc);
    const auto ch = mixer.addChannel (ida::InputId (1), ida::SignalType::Audio);
    CHECK (mixer.channelMainOut (ch) == ida::InputMixer::MainOutDest::Tape);
}

TEST_CASE ("InputMixer::busForId returns the bus for a known id, nullptr otherwise",
           "[input-mixer][bus-access]")
{
    ida::InputMixer mixer;
    const ida::BusId id = mixer.addBus (ida::BusConfig { 2, "Drum Bus", ida::BusKind::Bus });

    ida::Bus* bus = mixer.busForId (id);
    REQUIRE (bus != nullptr);
    REQUIRE (bus->id() == id);
    REQUIRE (bus->config().name == "Drum Bus");

    // Round-trip a control through the accessor (what the UI does).
    bus->setGain (0.3f);
    REQUIRE (mixer.busForId (id)->gain() == Catch::Approx (0.3f));

    REQUIRE (mixer.busForId (ida::BusId { 9999 }) == nullptr);
}

TEST_CASE ("InputMixer::renameBus updates plain buses + FX returns; rejects unknown ids",
           "[input-mixer][slice4][rename]")
{
    // Input side has no "master" concept — every bus is renameable. The only
    // reject path is an unknown id.
    ida::InputMixer mixer;
    const auto drums  = mixer.addBus (ida::BusConfig { 2, "Drums", ida::BusKind::Bus });
    const auto reverb = mixer.addFxReturn ("Reverb");

    // Unknown ids — engine rejects, lives unchanged.
    CHECK_FALSE (mixer.renameBus (ida::BusId { 0 },    "x"));   // 0 is invalid-sentinel
    CHECK_FALSE (mixer.renameBus (ida::BusId { 9999 }, "x"));

    // Plain bus rename — writes through to the live bus.
    REQUIRE (mixer.renameBus (drums, "Kick + Snare"));
    REQUIRE (mixer.busForId (drums) != nullptr);
    CHECK (mixer.busForId (drums)->config().name == "Kick + Snare");

    // FX return rename — same path; the BusKind is irrelevant to renameBus.
    REQUIRE (mixer.renameBus (reverb, "Plate Verb"));
    REQUIRE (mixer.busForId (reverb) != nullptr);
    CHECK (mixer.busForId (reverb)->config().name == "Plate Verb");

    // Persistence: both renamed buses survive export → import.
    const auto exported = mixer.exportGraphState();
    REQUIRE (exported.buses.size() == 2);
    CHECK (exported.buses[0].name == "Kick + Snare");
    CHECK (exported.buses[1].name == "Plate Verb");

    ida::InputMixer loaded;
    loaded.importGraphState (exported);
    REQUIRE (loaded.busForId (drums)  != nullptr);
    REQUIRE (loaded.busForId (reverb) != nullptr);
    CHECK (loaded.busForId (drums)->config().name  == "Kick + Snare");
    CHECK (loaded.busForId (reverb)->config().name == "Plate Verb");
}

TEST_CASE ("InputMixer: a freshly constructed mixer has exactly the primary tape (TapeId 1)",
           "[input-mixer][multi-tape]")
{
    using ida::InputMixer; using ida::TapeId;
    InputMixer mixer;
    CHECK (mixer.tapeCount() == 1);
    CHECK (mixer.hasTape (TapeId { 1 }));
    CHECK_FALSE (mixer.hasTape (TapeId { 2 }));
}

TEST_CASE ("InputMixer: addTape registers a routable terminal; removeTape unregisters it; primary is permanent",
           "[input-mixer][multi-tape]")
{
    using ida::InputMixer; using ida::TapeId;
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
    using ida::InputMixer; using ida::InputId; using ida::SignalType; using ida::TapeId;
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
    using ida::InputMixer; using ida::BusId; using ida::BusConfig; using ida::TapeId;
    InputMixer mixer;
    REQUIRE (mixer.addTape (TapeId { 2 }));
    const auto bus = mixer.addBus (BusConfig { 2, "Sub", ida::BusKind::Bus });

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
    using ida::InputMixer; using ida::TapeId;
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
    using ida::InputMixer;
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
    using ida::InputMixer; using ida::TapeId;
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
    using ida::InputMixer; using ida::BusId; using ida::BusConfig; using ida::TapeId;
    InputMixer mixer;
    RecordingTapeSink sink;
    mixer.setTapeSink (&sink);

    const auto bus = mixer.addBus (BusConfig { 2, "Sub", ida::BusKind::Bus });
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
    using ida::InputMixer;
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
    using ida::InputMixer; using ida::TapeId;
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
    ida::InputMixer mixer;
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.addTape (ida::TapeId { 2 }));
    REQUIRE (mixer.setChannelMainOutToTape (ch, ida::TapeId { 2 }));

    // Must NOT trip the mainOutSnapshot jassertfalse, and must record tape 2.
    const auto state = mixer.exportGraphState();
    REQUIRE (state.channels.size() == 1);
    const auto& mo = state.channels[0].mainOut;
    CHECK (mo.kind     == ida::MixerMainOut::Kind::Terminal);
    CHECK (mo.terminal == ida::MixerTerminalKind::Tape);
    CHECK (mo.tapeId   == 2);

    // Re-import into a fresh mixer (with tape 2 present) restores the route.
    ida::InputMixer restored;
    REQUIRE (restored.addTape (ida::TapeId { 2 }));
    restored.importGraphState (state);
    CHECK (restored.channelMainOutIsTape (ida::ChannelId (state.channels[0].channelId),
                                          ida::TapeId { 2 }));
}

TEST_CASE ("InputMixer reports which bus a node's main-out targets", "[input-mixer]")
{
    ida::InputMixer mixer;
    const auto a  = mixer.addBus (ida::BusConfig { 2, "A", ida::BusKind::Bus });
    const auto b  = mixer.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);

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
    ida::InputMixer mixer;
    const auto a = mixer.addBus (ida::BusConfig { 2, "A", ida::BusKind::Bus });
    const auto b = mixer.addBus (ida::BusConfig { 2, "B", ida::BusKind::Bus });
    REQUIRE (mixer.setBusMainOutToBus (a, b));           // a -> b
    REQUIRE (mixer.busMainOutToBusWouldCycle (b, a));    // b -> a would close the loop
    REQUIRE_FALSE (mixer.busMainOutToBusWouldCycle (a, b));
}

// Slice E2 — per-channel pre-fader send toggle on InputMixer (mixer-symmetry
// spec 2026-05-23 §Slice E2). Symmetric to OutputMixer: one toggle per
// channel covering all of that channel's sends. Pre-fader bypasses
// ChannelStrip::process (gain + mute) for the send tap so a muted channel
// still feeds its FX returns (reverb-on-cans / live-cue use cases).
TEST_CASE ("InputMixer::channelSendIsPreFader defaults to false; setter round-trips",
           "[input-mixer][send][pre-fader]")
{
    using ida::ChannelId;
    using ida::InputId;
    using ida::InputMixer;
    using ida::SignalType;

    InputMixer mixer;
    const auto ch = mixer.addChannel (InputId { 0 }, SignalType::Audio);

    CHECK_FALSE (mixer.channelSendIsPreFader (ch));

    mixer.setChannelSendIsPreFader (ch, true);
    CHECK (mixer.channelSendIsPreFader (ch));

    mixer.setChannelSendIsPreFader (ch, false);
    CHECK_FALSE (mixer.channelSendIsPreFader (ch));

    // Unknown ids return the safe default (false) rather than asserting.
    CHECK_FALSE (mixer.channelSendIsPreFader (ChannelId { 999 }));
}

TEST_CASE ("InputMixer pre-fader send bypasses channel mute on the FX-return tap",
           "[input-mixer][send][pre-fader][render]")
{
    using ida::ChannelStrip;
    using ida::InputId;
    using ida::InputMixer;
    using ida::SignalType;

    InputMixer mixer;
    const auto ch  = mixer.addChannel (InputId { 0 }, SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);

    // Mute the strip so the post-fader signal is silent.
    auto* chain = mixer.processingChainFor (ch);
    REQUIRE (chain != nullptr);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (chain);
    strip->setMuted (true);

    const auto rvb = mixer.addFxReturn ("RVB");
    // Isolate the send tap: kill the dry main-out, route the FX-return
    // straight to the hardware output, set a unity send.
    REQUIRE (mixer.setChannelMainOutToTape (ch));      // dry -> tape (off direct-out)
    REQUIRE (mixer.setChannelSend (ch, rvb, 1.0f));
    REQUIRE (mixer.setBusMainOutToHardwareOutput (rvb));

    // Engage pre-fader: the send tap should bypass the mute.
    mixer.setChannelSendIsPreFader (ch, true);

    constexpr int n = 16;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, n);

    // Pre-fader + muted strip: direct-out (= FX-return passthrough output)
    // still carries the source signal (no tape sink bound; dry tape path is
    // dropped). The default post-fader behavior would land silence here.
    CHECK (outL[0] != 0.0f);
    CHECK (outR[0] != 0.0f);
}

TEST_CASE ("InputMixer post-fader send respects channel mute (default)",
           "[input-mixer][send][pre-fader][render]")
{
    using ida::ChannelStrip;
    using ida::InputId;
    using ida::InputMixer;
    using ida::SignalType;

    InputMixer mixer;
    const auto ch  = mixer.addChannel (InputId { 0 }, SignalType::Audio);
    mixer.setChannelInputSource (ch, 0, 1, true);

    auto* chain = mixer.processingChainFor (ch);
    REQUIRE (chain != nullptr);
    auto* strip = static_cast<ChannelStrip<SignalType::Audio>*> (chain);
    strip->setMuted (true);

    const auto rvb = mixer.addFxReturn ("RVB");
    REQUIRE (mixer.setChannelMainOutToTape (ch));
    REQUIRE (mixer.setChannelSend (ch, rvb, 1.0f));
    REQUIRE (mixer.setBusMainOutToHardwareOutput (rvb));
    // Default mode = post-fader; the mute kills the send tap.

    constexpr int n = 16;
    std::vector<float> left (n, 0.5f), right (n, 0.5f);
    std::vector<float> outL (n, 0.0f), outR (n, 0.0f);
    const float* deviceIn[2] = { left.data(), right.data() };
    float* directOut[2] = { outL.data(), outR.data() };

    mixer.renderInputGraph (deviceIn, 2, directOut, 2, n);

    for (float v : outL) CHECK (v == Catch::Approx (0.0f));
    for (float v : outR) CHECK (v == Catch::Approx (0.0f));
}

TEST_CASE ("InputMixer export/import round-trips the preFaderSends flag",
           "[input-mixer][send][pre-fader][persistence]")
{
    using ida::ChannelId;
    using ida::InputDescriptor;
    using ida::InputId;
    using ida::InputKind;
    using ida::InputMixer;
    using ida::SignalType;
    using ida::TapeId;

    InputMixer source;
    const InputDescriptor desc {
        TapeId { 1 }, InputKind::Audio, std::string ("In 1"), std::optional<int> (0)
    };
    source.registerInput (InputId { 1 }, desc);

    const auto chA = source.addChannel (InputId { 1 }, SignalType::Audio);
    const auto chB = source.addChannel (InputId { 1 }, SignalType::Audio);
    source.setChannelSendIsPreFader (chA, true);
    // chB stays at default false.

    const auto exported = source.exportGraphState();
    REQUIRE (exported.channels.size() == 2);
    // Channels are exported in addChannel order; chA is index 0.
    const auto findCh = [&] (std::int64_t id)
    {
        for (const auto& c : exported.channels) if (c.channelId == id) return c;
        return ida::InputChannelState {};
    };
    CHECK (findCh (chA.value()).preFaderSends);
    CHECK_FALSE (findCh (chB.value()).preFaderSends);

    InputMixer loaded;
    loaded.importGraphState (exported);
    CHECK (loaded.channelSendIsPreFader (chA));
    CHECK_FALSE (loaded.channelSendIsPreFader (chB));
    CHECK (loaded.exportGraphState() == exported);
}

TEST_CASE ("InputMixer round-trips bus pan / width / gain / muted",
           "[InputMixer][persistence]")
{
    ida::InputMixer mixer;
    const auto busId = mixer.addBus (ida::BusConfig { 2, "AuxA", ida::BusKind::Bus });
    auto* bus = mixer.busForId (busId);
    REQUIRE (bus != nullptr);
    bus->setGain  (0.5f);
    bus->setMuted (true);
    bus->setPan   (0.25f);
    bus->setWidth (1.5f);

    const auto state = mixer.exportGraphState();
    ida::InputMixer restored;
    restored.importGraphState (state);

    auto* restoredBus = restored.busForId (busId);
    REQUIRE (restoredBus != nullptr);
    CHECK_FALSE (restoredBus->gain()  != 0.5f);
    CHECK       (restoredBus->muted());
    CHECK_FALSE (restoredBus->pan()   != 0.25f);
    CHECK_FALSE (restoredBus->width() != 1.5f);
}
