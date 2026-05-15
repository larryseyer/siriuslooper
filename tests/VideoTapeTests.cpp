// Tests for the video-tape data model (white paper Parts 5.3, 6.2). The tape
// is just `Tape<VideoFrame>` — every Sirius tape shares the same template —
// so these tests focus on the *video-specific* claims: the metadata each
// frame carries is correct, the per-frame duration accommodates variable-rate
// streams, and `findFrameAt` returns the most recent frame ≤ the query time
// (the minimal nearest-frame-on-display model, white paper Part 5.3).
#include "sirius/VideoTape.h"

#include "sirius/Position.h"
#include "sirius/Rational.h"
#include "sirius/TapeId.h"

#include <catch2/catch_test_macros.hpp>

using sirius::Position;
using sirius::Rational;
using sirius::TapeId;
using sirius::VideoFrame;
using sirius::VideoPixelFormat;
using sirius::VideoTape;

namespace
{
    VideoTape::Event frameEvent (TapeId tape, Rational lmcSeconds,
                                 int width = 1920, int height = 1080)
    {
        VideoFrame f;
        f.metadata.width  = width;
        f.metadata.height = height;
        f.metadata.pixelFormat = VideoPixelFormat::Yuv420p;
        f.metadata.presentationLmcSeconds = lmcSeconds;
        f.metadata.frameDurationSeconds   = Rational (1, 30);
        return { Position (lmcSeconds), lmcSeconds, tape, std::move (f) };
    }
}

TEST_CASE ("a video tape carries frames with their presentation timestamps",
           "[videotape]")
{
    const TapeId t (7);
    VideoTape tape (t);
    tape.append (frameEvent (t, Rational (0)));
    tape.append (frameEvent (t, Rational (1, 30)));
    tape.append (frameEvent (t, Rational (2, 30)));

    REQUIRE (tape.size() == 3);
    CHECK (tape.events()[1].payload.metadata.presentationLmcSeconds == Rational (1, 30));
    CHECK (tape.events()[2].payload.metadata.width == 1920);
    CHECK (tape.events()[2].payload.metadata.pixelFormat == VideoPixelFormat::Yuv420p);
}

TEST_CASE ("findFrameAt returns the most recent frame at or before the query",
           "[videotape]")
{
    // The display rule: show the latest frame the source produced. This is
    // what produces a stutter rather than a blank when the source falls
    // behind — the right behaviour for live playback.
    const TapeId t (1);
    VideoTape tape (t);
    tape.append (frameEvent (t, Rational (0)));
    tape.append (frameEvent (t, Rational (1, 30)));
    tape.append (frameEvent (t, Rational (2, 30)));

    SECTION ("a query before the first frame returns nullptr")
    {
        CHECK (sirius::findFrameAt (tape, Rational (-1, 1)) == nullptr);
    }

    SECTION ("a query exactly on a frame returns that frame")
    {
        const auto* f = sirius::findFrameAt (tape, Rational (1, 30));
        REQUIRE (f != nullptr);
        CHECK (f->metadata.presentationLmcSeconds == Rational (1, 30));
    }

    SECTION ("a query between frames returns the earlier frame")
    {
        const auto* f = sirius::findFrameAt (tape, Rational (3, 60)); // = 1.5/30
        REQUIRE (f != nullptr);
        CHECK (f->metadata.presentationLmcSeconds == Rational (1, 30));
    }

    SECTION ("a query past the last frame holds on the last frame")
    {
        const auto* f = sirius::findFrameAt (tape, Rational (100));
        REQUIRE (f != nullptr);
        CHECK (f->metadata.presentationLmcSeconds == Rational (2, 30));
    }
}

TEST_CASE ("findFrameAt on an empty tape returns nullptr", "[videotape]")
{
    VideoTape tape (TapeId (1));
    CHECK (sirius::findFrameAt (tape, Rational (0)) == nullptr);
}
