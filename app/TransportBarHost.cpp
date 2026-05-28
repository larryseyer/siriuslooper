#include "TransportBarHost.h"

namespace ida
{

TransportBarHost::TransportBarHost (OttoHost& host)
    : host_ (host)
{
    bar_.addListener (this);
    addAndMakeVisible (bar_);

    // Spectrum configure is deferred to `timerCallback`: at ctor time the
    // master publishers may not be wired yet (T8 attaches them between
    // construction and the first tick), so `spectrumBinCount()` would
    // return 0 and bake a zero-bin bar. The timer detects the bin-count
    // change on the next tick and configures the bar with the host's real
    // sample rate.

    host_.addTransportListener (this);

    startTimerHz (kRefreshRateHz);
}

TransportBarHost::~TransportBarHost()
{
    // Reverse construction order: timer → host listener → bar listener.
    stopTimer();
    host_.removeTransportListener (this);
    bar_.removeListener (this);
}

void TransportBarHost::resized()
{
    bar_.setBounds (getLocalBounds());
}

otto::ui::TransportBar& TransportBarHost::getBar() noexcept
{
    return bar_;
}

void TransportBarHost::playPauseClicked()
{
    // OTTO is the source of truth — call host_.play()/stop() based on
    // the bar's CURRENT state (which mirrors OTTO via onOttoTransport).
    if (bar_.getTransportState() == otto::ui::TransportState::Playing)
        host_.stop();
    else
        host_.play();
    // Duplicate stop() during OTTO's stop-message-posted-but-not-yet-emitted window is idempotent on OTTO's side.
}

void TransportBarHost::stopClicked()
{
    host_.stop();
}

void TransportBarHost::tempoChanged (double newTempo)
{
    host_.setTempo (newTempo);
}

void TransportBarHost::tapTempo()
{
    host_.tapTempo();
}

void TransportBarHost::onOttoTransport (const TransportSnapshot& snap)
{
    using Kind = TransportSnapshot::Kind;
    switch (snap.kind)
    {
        case Kind::Started:
            bar_.setTransportState (otto::ui::TransportState::Playing);
            break;
        case Kind::Stopped:
            bar_.setTransportState (otto::ui::TransportState::Stopped);
            break;
        case Kind::BpmChanged:
            bar_.setTempo (snap.bpm);
            break;
        case Kind::TimeSigChanged:
            // TransportBar has no setTimeSignature today; no-op until added.
            break;
    }
}

void TransportBarHost::timerCallback()
{
    const int currentBinCount = host_.spectrumBinCount();
    if (currentBinCount != configuredBinCount_)
    {
        const double sr = host_.getPreparedSampleRate();
        // 48 kHz fallback only if the host hasn't been prepared yet (sentinel
        // 0.0) — the bar still needs a non-zero sample rate to avoid divide-
        // by-zero in its frequency mapping. Once `prepare()` lands, the real
        // rate takes over on the next bin-count change.
        bar_.configureSpectrum (currentBinCount,
                                sr > 0.0 ? sr : 48000.0);
        configuredBinCount_ = currentBinCount;
    }

    const auto m = host_.snapshotMaster();
    bar_.setMasterLevels (m.leftDb, m.rightDb);
    bar_.setMasterPeak   (m.peakDb);
    bar_.setMasterLUFS   (m.lufs);

    for (int bin = 0; bin < currentBinCount; ++bin)
        bar_.setSpectrumBin (bin, host_.spectrumBinDb (bin));
}

} // namespace ida
