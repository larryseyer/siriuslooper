// Tests for ida::OttoHost::play / stop / setTempo / tapTempo and the
// master-mix-point snapshot accessors (S3a T5). The transport methods
// forward to OTTOProcessor::sendToAudioThread, which pushes into a
// lock-free UI→audio queue — the actual TransportTracker state change
// only happens once OTTO's processBlock runs and drains the queue. The
// IdaTests harness doesn't pump audio blocks here, so these tests pin
// the seam that matters for IDA: the methods are callable without
// crashing, the master-snapshot accessors return the documented sentinel
// before setMasterPublishers is called, and reading through wired
// publishers reflects the data the publishers store.
//
// `juce::ScopedJuceInitialiser_GUI` is required because OttoHost owns a
// `juce::Timer` whose ctor touches the MessageManager (same pattern as
// the sibling OttoHost tests).

#include "ida/OttoHost.h"
#include "ida/MasterMeter.h"
#include "ida/MasterSpectrum.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using ida::OttoHost;

namespace
{

constexpr double kTestSampleRate = 48000.0;
constexpr int    kTestBlockSize  = 256;

} // namespace

TEST_CASE ("OttoHost transport-control methods are callable before prepare without crashing",
           "[otto-host-transport-control]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;

    // None of these are noexcept by signature, but they must not throw.
    host.play();
    host.stop();
    host.setTempo (140.0);
    host.tapTempo();  // documented no-op until OTTO adds an audio-thread path
}

TEST_CASE ("OttoHost transport-control methods are callable after prepare without crashing",
           "[otto-host-transport-control]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);

    host.play();
    host.setTempo (96.0);
    host.stop();
    host.tapTempo();
}

TEST_CASE ("OttoHost::snapshotMaster returns the documented sentinel before setMasterPublishers",
           "[otto-host-transport-control]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    const auto s = host.snapshotMaster();
    CHECK (s.leftDb  == -100.0f);
    CHECK (s.rightDb == -100.0f);
    CHECK (s.peakDb  == -100.0f);
    CHECK (s.lufs    == -100.0f);

    CHECK (host.spectrumBinCount() == 0);
    CHECK (host.spectrumBinDb (0)  == -100.0f);
    CHECK (host.spectrumBinDb (-1) == -100.0f);
    CHECK (host.spectrumBinDb (4096) == -100.0f);
}

TEST_CASE ("OttoHost::snapshotMaster forwards to the wired MasterMeter publisher",
           "[otto-host-transport-control]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    ida::MasterMeter    meter;
    ida::MasterSpectrum spec;
    meter.prepare (kTestSampleRate, kTestBlockSize);
    spec.prepare  (kTestSampleRate, kTestBlockSize, 256);

    OttoHost host;
    host.setMasterPublishers (meter, spec);

    // Pre-publish: meter snapshot is its default (dB floor for an
    // un-published meter). The point of this case is that the host's
    // forwarder reads through to the same Snapshot the meter vends —
    // any non-zero, published signal must appear via snapshotMaster.
    juce::AudioBuffer<float> buf (2, kTestBlockSize);
    buf.clear();
    for (int i = 0; i < kTestBlockSize; ++i)
    {
        buf.setSample (0, i, 0.5f);
        buf.setSample (1, i, 0.25f);
    }
    meter.publish (buf.getReadPointer (0), buf.getReadPointer (1), buf.getNumSamples());

    const auto direct = meter.snapshot();
    const auto via    = host.snapshotMaster();
    CHECK (via.leftDb  == Catch::Approx (direct.leftDb));
    CHECK (via.rightDb == Catch::Approx (direct.rightDb));
    CHECK (via.peakDb  == Catch::Approx (direct.peakDb));
    CHECK (via.lufs    == Catch::Approx (direct.lufs));

    // Spectrum forwarder advertises the same numBins as the wired publisher.
    CHECK (host.spectrumBinCount() == spec.numBins());
    // Out-of-range bin always returns the -100 dB floor (per MasterSpectrum
    // contract), regardless of whether the publisher has been driven yet.
    CHECK (host.spectrumBinDb (-1) == -100.0f);
    CHECK (host.spectrumBinDb (spec.numBins() + 1024) == -100.0f);
}
