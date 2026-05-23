#pragma once

#include "ida/Rational.h"

#include <cstdint>
#include <vector>

namespace sirius
{

/// The video membrane (white paper Part 5.3): the boundary at which a video
/// source's natural frame rate meets whatever rate is needed for storage,
/// processing, or display. The minimal strategy — the one IDA commits to
/// at M6 — is nearest-frame selection: at each query moment, the source
/// frame whose presentation time is closest is shown. Motion-compensated
/// frame interpolation is a deliberate later refinement; the architecture
/// above the membrane is identical either way.
///
/// This class is the math half of that membrane — exact Rational arithmetic,
/// no FFmpeg, no decoded bytes. Given a source frame rate, it answers
/// "what's the index of the nearest source frame at LMC time T?", and the
/// inverse — "when does source frame K appear?". The pixel-bytes half of
/// the membrane (decode, swscale, upload to GPU) is the FFmpeg-bound work
/// catalogued in todo.md against the M0 spike.
class FrameMembrane
{
public:
    /// `sourceFps` is the source's nominal frame rate, as a Rational so the
    /// awkward broadcast rates (23.976 = 24000/1001, 29.97 = 30000/1001,
    /// 59.94 = 60000/1001) are representable exactly rather than rounded.
    /// `sourceStartLmcSeconds` is when source frame 0 was presented.
    /// Throws std::invalid_argument if `sourceFps` is not positive.
    FrameMembrane (Rational sourceFps, Rational sourceStartLmcSeconds = Rational());

    Rational sourceFps()        const noexcept { return sourceFps_; }
    Rational frameDuration()    const noexcept { return frameDuration_; }
    Rational sourceStart()      const noexcept { return sourceStart_; }

    /// The presentation time of source frame `index`, in LMC seconds. Exact.
    Rational presentationTimeOf (std::int64_t frameIndex) const;

    /// The source-frame index closest in time to `lmcTime` — nearest-frame
    /// selection. Negative values indicate "before the source started";
    /// values >= total source frames indicate "past the source's end" and
    /// are the caller's signal to either hold the last frame or blank.
    std::int64_t nearestFrameIndex (Rational lmcTime) const;

private:
    Rational sourceFps_;
    Rational frameDuration_; ///< memoised 1 / sourceFps_
    Rational sourceStart_;
};

/// Maps a contiguous run of target-rate frames to their nearest source-rate
/// frame indices — the bulk frame-rate-conversion query. Given a source FPS
/// (e.g. 24), a target FPS (e.g. 30), and a target-frame count `N`, returns
/// a vector of `N` source indices, one per target frame, computed by
/// nearest-frame selection. 24 → 30 stuffs (some source frames repeat);
/// 30 → 24 drops (some source frames are skipped); equal rates pass through.
///
/// `sourceStartLmcSeconds` and `targetStartLmcSeconds` align the two
/// timelines; pass both as zero when the source and target start at the
/// same moment. Throws std::invalid_argument if either FPS is not positive
/// or if `targetFrameCount` is negative.
std::vector<std::int64_t> convertFrameRate (
    Rational sourceFps,
    Rational targetFps,
    std::int64_t targetFrameCount,
    Rational sourceStartLmcSeconds = Rational(),
    Rational targetStartLmcSeconds = Rational());

} // namespace sirius
