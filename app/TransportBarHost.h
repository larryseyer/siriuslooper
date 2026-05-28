#pragma once

#include "ida/OttoHost.h"
#include "ida/IOttoTransportListener.h"

#include "components/TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ida
{

/// Wrapper Component that owns IDA's persistent transport bar
/// (`otto::ui::TransportBar`) and bridges it to the embedded `OttoHost`.
///
/// Responsibilities (S3a T6):
///   * Forward bar listener events (play/stop/tempo/tap) to `OttoHost`.
///   * Mirror OTTO transport state into the bar via `IOttoTransportListener`.
///   * Pull the master meter + spectrum snapshot at ~30 Hz and push it into
///     the bar so the LEDs / spectrum stay current.
class TransportBarHost
    : public juce::Component,
      public otto::ui::TransportBarListener,
      public IOttoTransportListener,
      private juce::Timer
{
public:
    explicit TransportBarHost (OttoHost& host);
    ~TransportBarHost() override;

    TransportBarHost (const TransportBarHost&)            = delete;
    TransportBarHost& operator= (const TransportBarHost&) = delete;
    TransportBarHost (TransportBarHost&&)                 = delete;
    TransportBarHost& operator= (TransportBarHost&&)      = delete;

    /// juce::Component
    void resized() override;

    /// Test seam — exposes the embedded TransportBar for assertion.
    otto::ui::TransportBar& getBar() noexcept;

    /// otto::ui::TransportBarListener
    void playPauseClicked() override;
    void stopClicked() override;
    void tempoChanged (double newTempo) override;
    void tapTempo() override;

    /// ida::IOttoTransportListener
    void onOttoTransport (const TransportSnapshot& snapshot) override;

private:
    /// juce::Timer — `kRefreshRateHz`; pulls meter + spectrum from OttoHost
    /// and pushes the latest snapshot into the bar.
    void timerCallback() override;

    /// Bar refresh cadence. Matches IDA's other message-thread UI polls.
    static constexpr int kRefreshRateHz = 30;

    OttoHost& host_;
    otto::ui::TransportBar bar_;

    /// Last bin count we configured `bar_` with. Tracked so the timer can
    /// re-call `configureSpectrum` exactly when the host's bin count first
    /// becomes non-zero (i.e. after T8 calls `setMasterPublishers`).
    int configuredBinCount_ { 0 };
};

} // namespace ida
