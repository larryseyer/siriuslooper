#pragma once

namespace sirius
{

/// The categories of work the system does, in descending priority order (white
/// paper Part 13.3, rule 4). Audio is sacred; the other three are shed under
/// load, lowest priority first.
enum class Workload
{
    Audio,    ///< the production signal path — never shed, rule 1
    Analyzer, ///< pitch/beat analysis and other listening work
    Ui,       ///< interface redraws and meters
    Video     ///< video frame rendering — the heaviest, shed first
};

/// Runtime overload protection (white paper Part 13.3). The system continuously
/// reports how loaded the audio callback is; this class turns that signal into
/// a decision about which non-audio work to shed. It enforces the unbreakable
/// rules: audio is never shed (rule 1), and degradation is explicit state the
/// UI can read and announce, never silent (rule 3).
///
/// Shedding follows a strict priority ladder — Video sheds first, then Ui, then
/// Analyzer — each with a hysteresis band so that load hovering around a
/// threshold does not flap the corresponding work on and off every callback.
/// This is a pure state machine: no audio, no threading, no JUCE — so the
/// ladder is exhaustively unit-testable.
class OverloadProtection
{
public:
    /// Constructs with nothing shed — the system starts at full fidelity.
    OverloadProtection() = default;

    /// Feeds in the latest audio-callback load, as a fraction of the callback's
    /// time budget. Values above 1.0 are meaningful — they signal the callback
    /// is overrunning — and are not clamped. A negative value is a programming
    /// error enforced via assert (debug) / undefined behavior (release).
    void reportLoad (double audioCallbackLoad) noexcept;

    /// Whether `workload` is currently shed. Always false for Workload::Audio.
    bool isShed (Workload workload) const;

    /// How many workloads are currently shed (0 to 3).
    int shedCount() const;

    /// The most recent load passed to reportLoad, or 0.0 if none yet.
    double lastReportedLoad() const noexcept { return lastLoad_; }

private:
    bool shedVideo_    { false };
    bool shedUi_       { false };
    bool shedAnalyzer_ { false };
    double lastLoad_   { 0.0 };
};

} // namespace sirius
