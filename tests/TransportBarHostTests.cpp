// Tests for ida::TransportBarHost (S3a T6) — the wrapper Component that
// owns IDA's persistent transport bar (otto::ui::TransportBar) and bridges
// it to the embedded OttoHost.
//
// The second case ("playPauseClicked starts OTTO when stopped") in the
// plan asserts l.startedSeen via the IOttoTransportListener fan-out, but
// in the headless IdaTests harness that fan-out is unreachable:
// OttoHost::play() posts an audio-thread message that only emits the
// TransportEvent::Started from inside OTTO's processBlock — which the
// test harness never pumps. The matching pattern is established by
// OttoHostTransportControlTests.cpp, which pins the seam ("callable
// without crashing") instead. We do the same here, plus a direct
// IOttoTransportListener::onOttoTransport invocation to verify the
// IDA→bar mirror direction — which is T6's actual responsibility.
//
// `juce::ScopedJuceInitialiser_GUI` is required because TransportBarHost
// is itself a juce::Component + juce::Timer, and OttoHost owns a
// juce::Timer whose ctor touches the MessageManager.
//
// The TransportBar (via FaderMeter / SpectrumDisplay / etc.) constructs
// Typefaces during ctor. Without an OTTOLookAndFeel installed as the
// default look-and-feel, those Typefaces end up registered with JUCE's
// default LookAndFeel singleton, which is destroyed after
// ScopedJuceInitialiser_GUI has already torn JUCE down — and the
// CoreTextTypeface dtor then terminates with `mutex lock failed: Invalid
// argument`. Production wires this in `app/Main.cpp` (setDefaultLookAndFeel
// + reset to nullptr at shutdown); the `ScopedJuceTestEnv` RAII guard
// below mirrors that contract so each test exits cleanly.
//
// The fixture snapshots the previous default LookAndFeel in its ctor and
// restores it in its dtor, instead of unconditionally setting null. That
// protects future tests from inheriting a null default L&F if this fixture
// is composed with another that already installed one.

#include "TransportBarHost.h"
#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"

#include "OTTOLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>

namespace
{

struct ScopedJuceTestEnv
{
    ScopedJuceTestEnv()
    {
        previousDefault_ = &juce::LookAndFeel::getDefaultLookAndFeel();
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    }

    ~ScopedJuceTestEnv()
    {
        juce::LookAndFeel::setDefaultLookAndFeel (previousDefault_);
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    otto::OTTOLookAndFeel           lookAndFeel;
    juce::LookAndFeel*              previousDefault_ { nullptr };
};

} // namespace

TEST_CASE ("TransportBarHost constructs against OttoHost and exposes the bar",
           "[transport-bar-host]")
{
    ScopedJuceTestEnv env;

    ida::OttoHost host;
    host.prepare (48000.0, 256);

    ida::TransportBarHost barHost (host);
    auto& bar = barHost.getBar();
    // A reference cannot be null in well-defined C++ — the assertion that
    // matters is the bar's local default tempo is positive (verifies it
    // was constructed, not e.g. zero-initialized via a stale this pointer).
    CHECK (bar.getTempo() > 0.0);
}

TEST_CASE ("TransportBarHost::playPauseClicked forwards to OttoHost without crashing",
           "[transport-bar-host]")
{
    ScopedJuceTestEnv env;

    ida::OttoHost host;
    host.prepare (48000.0, 256);
    ida::TransportBarHost barHost (host);

    // The actual TransportEvent::Started fan-out only fires once OTTO's
    // processBlock runs; the test harness can't pump audio. We pin the
    // seam instead — the wiring compiles and the forward path is
    // exercised, matching the OttoHostTransportControlTests convention.
    barHost.playPauseClicked();
    barHost.stopClicked();
    barHost.tempoChanged (96.0);
    barHost.tapTempo();
}

TEST_CASE ("TransportBarHost::onOttoTransport mirrors OTTO state into the bar",
           "[transport-bar-host]")
{
    ScopedJuceTestEnv env;

    ida::OttoHost host;
    host.prepare (48000.0, 256);
    ida::TransportBarHost barHost (host);

    // Drive the IDA→bar mirror direction directly via the public
    // IOttoTransportListener override — this is T6's actual responsibility
    // (mirror OTTO transport state into the bar), independent of OTTO's
    // audio thread.
    using S = ida::TransportSnapshot;

    barHost.onOttoTransport (S { S::Kind::Started, 120.0, true, 4, 4 });
    CHECK (barHost.getBar().getTransportState() == otto::ui::TransportState::Playing);

    barHost.onOttoTransport (S { S::Kind::Stopped, 120.0, false, 4, 4 });
    CHECK (barHost.getBar().getTransportState() == otto::ui::TransportState::Stopped);

    barHost.onOttoTransport (S { S::Kind::BpmChanged, 144.0, false, 4, 4 });
    CHECK (barHost.getBar().getTempo() > 143.0);
    CHECK (barHost.getBar().getTempo() < 145.0);

    // TimeSigChanged is a documented no-op (TransportBar has no
    // setTimeSignature today) — just confirm it doesn't crash.
    barHost.onOttoTransport (S { S::Kind::TimeSigChanged, 144.0, false, 3, 4 });
}
