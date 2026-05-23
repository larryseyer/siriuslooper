#pragma once

#include "ida/Rational.h"
#include "ida/Tape.h"
#include "ida/VideoFrame.h"

namespace ida
{

/// A tape of video frames. Just `Tape<VideoFrame>` — every tape in IDA
/// shares the same Tape<> template (white paper Part 6.2); only the payload
/// type changes. The alias exists so call sites read "VideoTape" instead of
/// repeating the template.
using VideoTape = Tape<VideoFrame>;

/// The frame to display at `lmcTime` — the most recent frame whose
/// presentation timestamp is at or before the query time. Returns the
/// pointer into the tape's storage on hit, or `nullptr` if the tape is
/// empty or the query precedes every frame. This is the simplest playback
/// model: the screen always shows the last frame the source produced,
/// stuttering rather than blanking when the source falls behind (white
/// paper Part 5.3 — nearest-frame selection, the minimal video-membrane
/// strategy).
inline const VideoFrame* findFrameAt (const VideoTape& tape, Rational lmcTime)
{
    const VideoFrame* current = nullptr;
    for (const auto& event : tape.events())
    {
        if (event.payload.metadata.presentationLmcSeconds > lmcTime)
            break;
        current = &event.payload;
    }
    return current;
}

} // namespace ida
