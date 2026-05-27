#pragma once

namespace ida
{

/// One transport state-change observed at OTTO's `TransportTracker`, translated
/// into an IDA-flavoured POD so listeners never see `otto::` types. This is the
/// payload `IOttoTransportListener::onOttoTransport` receives on the message
/// thread (the OttoHost's drainer fans events out of an SPSC ring; see
/// `OttoHost.cpp` for the audio→message marshal). M-OTTO-3 contract.
struct TransportSnapshot
{
    enum class Kind { Started, Stopped, BpmChanged, TimeSigChanged };

    Kind   kind        { Kind::Stopped };
    double bpm         { 120.0 };
    bool   isPlaying   { false };
    int    timeSigNum  { 4 };
    int    timeSigDen  { 4 };
};

/// Message-thread receiver of `TransportSnapshot` events. Implementations
/// must be cheap (the OttoHost drainer iterates all listeners synchronously
/// on the timer tick); heavy work should be dispatched off the listener
/// callback. No RT-safety requirement — the drainer runs on the message
/// thread.
class IOttoTransportListener
{
public:
    virtual ~IOttoTransportListener() = default;

    /// Called once per drained snapshot, message-thread, in publication order
    /// per OTTO's EventBus. A burst of events emitted in one OTTO audio block
    /// is delivered in order on the next drainer tick.
    virtual void onOttoTransport (const TransportSnapshot& snapshot) = 0;
};

} // namespace ida
