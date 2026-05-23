#pragma once

#include "ida/Rational.h"

#include <cstdint>
#include <vector>

namespace ida
{

/// The pixel layouts the video tape format supports. The list is deliberately
/// small: planar YUV for cameras and intra-frame codecs, packed RGBA for
/// screen captures and rendered overlays. Anything more exotic — 10-bit YUV,
/// V-Log, Apple ProRes 4444 — converts to one of these at the FFmpeg
/// boundary, on the operator-verified half of M6.
enum class VideoPixelFormat
{
    Yuv420p, ///< planar 4:2:0, the camera and intra-frame-codec default
    Yuv422p, ///< planar 4:2:2, broadcast capture
    Rgba8,   ///< packed 8-bit RGBA, screen capture and overlays
    Bgra8,   ///< packed 8-bit BGRA, the Apple-native screen layout
    Nv12     ///< semi-planar 4:2:0, the GPU-decode default on most platforms
};

/// What a single video frame carries, independent of its bytes. The metadata
/// rides on the tape so the membrane (white paper Part 5.3) and the renderer
/// can answer "which frame at LMC time T" without decoding anything.
struct VideoFrameMetadata
{
    int              width  { 0 };
    int              height { 0 };
    VideoPixelFormat pixelFormat { VideoPixelFormat::Yuv420p };

    /// When this frame should be on screen, in LMC seconds — exact, the way
    /// every conceptual-time stamp in the system is exact.
    Rational         presentationLmcSeconds;

    /// One source-frame's duration: 1 / sourceFps. Carried per-frame so a
    /// variable-frame-rate stream is representable without a separate header.
    Rational         frameDurationSeconds;
};

/// A single video frame on the tape — metadata plus the codec-specific or
/// raw pixel bytes the FFmpeg layer produces. The data model never inspects
/// `pixels`; the byte payload is opaque to everything above the membrane.
/// (White paper Part 6.2 — all tapes share one structure; only the payload
/// differs.)
struct VideoFrame
{
    VideoFrameMetadata        metadata;
    std::vector<std::uint8_t> pixels;
};

} // namespace ida
