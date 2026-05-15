#include "sirius/OverloadProtection.h"

#include <stdexcept>

namespace sirius
{

namespace
{
    /// One workload's hysteresis band. `shedAbove` and `restoreBelow` straddle a
    /// dead zone: load inside the band leaves the current decision untouched, so
    /// load hovering near a threshold does not flap the work on and off.
    struct Band
    {
        double shedAbove;
        double restoreBelow;
    };

    // The ladder. Video sheds earliest and restores last; Analyzer is held
    // longest because listening work is more musically load-bearing than a
    // video frame or an interface redraw. The bands tile without overlap:
    // [0.65,0.75] video, [0.75,0.85] ui, [0.85,0.95] analyzer.
    constexpr Band videoBand    { 0.75, 0.65 };
    constexpr Band uiBand       { 0.85, 0.75 };
    constexpr Band analyzerBand { 0.95, 0.85 };

    bool applyBand (bool currentlyShed, double load, const Band& band)
    {
        if (! currentlyShed && load > band.shedAbove)
            return true;
        if (currentlyShed && load < band.restoreBelow)
            return false;
        return currentlyShed;
    }
}

void OverloadProtection::reportLoad (double audioCallbackLoad)
{
    if (audioCallbackLoad < 0.0)
        throw std::invalid_argument (
            "sirius::OverloadProtection: audio-callback load must not be negative");

    lastLoad_ = audioCallbackLoad;

    shedVideo_    = applyBand (shedVideo_,    audioCallbackLoad, videoBand);
    shedUi_       = applyBand (shedUi_,       audioCallbackLoad, uiBand);
    shedAnalyzer_ = applyBand (shedAnalyzer_, audioCallbackLoad, analyzerBand);
}

bool OverloadProtection::isShed (Workload workload) const
{
    switch (workload)
    {
        case Workload::Audio:    return false; // rule 1 — audio is never shed
        case Workload::Analyzer: return shedAnalyzer_;
        case Workload::Ui:       return shedUi_;
        case Workload::Video:    return shedVideo_;
    }
    return false;
}

int OverloadProtection::shedCount() const
{
    return (shedVideo_ ? 1 : 0) + (shedUi_ ? 1 : 0) + (shedAnalyzer_ ? 1 : 0);
}

} // namespace sirius
