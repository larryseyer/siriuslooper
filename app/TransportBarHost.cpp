#include "TransportBarHost.h"

namespace ida
{

TransportBarHost::TransportBarHost (OttoHost& host)
    : host_ (host)
{
    bar_.addListener (this);
    addAndMakeVisible (bar_);

    // Configure the bar's spectrum to match the publisher's resolution
    // wired into OttoHost via setMasterPublishers. If the publishers
    // haven't been wired yet the bin count is 0 (sentinel) — that's a
    // legal value the bar will not paint a spectrum for.
    bar_.configureSpectrum (host_.spectrumBinCount(), 48000.0);

    host_.addTransportListener (this);

    startTimerHz (30);
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
    const auto m = host_.snapshotMaster();
    bar_.setMasterLevels (m.leftDb, m.rightDb);
    bar_.setMasterPeak   (m.peakDb);
    bar_.setMasterLUFS   (m.lufs);

    const int numBins = host_.spectrumBinCount();
    for (int bin = 0; bin < numBins; ++bin)
        bar_.setSpectrumBin (bin, host_.spectrumBinDb (bin));
}

} // namespace ida
