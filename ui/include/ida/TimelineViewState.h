#pragma once

#include "ida/Constituent.h"
#include "ida/InputDescriptor.h"
#include "ida/Rational.h"
#include "ida/TapeId.h"
#include "ida/TempoMap.h"

#include <string>
#include <vector>

namespace sirius
{

/// One Pill on the timeline — the UI rendering of a Phrase Constituent
/// (a Constituent carrying PhraseMetadata, white paper Part VIII). A Pill
/// surfaces the four glanceable atoms from the OTTO 4-corner contract:
/// loop count, phrase-loop on/off, entrance, exit, plus the phrase name.
///
/// A Pill anchors to its **primary tape** (the TapeId most frequently
/// referenced by its Loop descendants); `memberTapes` is the union, used
/// by the renderer to draw a thin membership outline on secondary rows.
/// Single-tape phrases — the dominant case — have one entry in
/// `memberTapes` equal to `primaryTape` and need no outline.
struct PillState
{
    /// The phrase Constituent's id, so a click on a Pill maps back into
    /// the session graph for selection / edit.
    ConstituentId id { 0 };
    std::string   name;

    /// The Pill's time span on the song timeline, in LMC seconds. Computed
    /// from the Phrase's accumulated conceptual position through the root
    /// TempoMap — the same path PerformanceViewState walks.
    Rational startLmcSeconds;
    Rational endLmcSeconds;

    /// Anchor row + membership for the multi-tape future. For a single-
    /// tape Phrase, `memberTapes == { primaryTape }`.
    TapeId              primaryTape { 0 };
    std::vector<TapeId> memberTapes;

    /// The four-corner OTTO atoms.
    int  loopCount        { 0 };
    bool phraseLoopActive { false };
    std::string entranceName;
    std::string exitName;

    /// For shared placements: the ids of the other wrappers that share the
    /// same underlying Phrase ChildPtr. The renderer draws a tie-bar across
    /// the wrappers in this set ∪ {this Pill's id}. Empty for bare Phrases
    /// and for forked placements.
    std::vector<ConstituentId> sharedSiblings;

    /// True iff this placement has instance-only overlay Loops (children at
    /// index ≥ 1 on the wrapper). Renderer draws the overlay-dot marker.
    bool hasOverlays { false };

    /// True iff this Pill represents a placement that was forked from a
    /// previously-shared one. Detected via PhraseMetadata::role ==
    /// "forked-placement". Renderer draws the prime mark.
    bool isForked { false };
};

/// One row on the timeline — one Tape + InputDescriptor + transient UI
/// state. Per the data-vs-UI split the user fixed in this milestone, no
/// `Track` type exists in `core/`; the row is the UI's word for an input
/// topology entry, materialized only at view-selection time.
struct TrackStripState
{
    TapeId      tapeId      { 0 };
    InputKind   kind        { InputKind::Audio };
    std::string displayName;

    /// Per-tape arm state — the primary capture gesture in the refined
    /// Mockup A. The bottom-bar Mark In/Out targets the focused tape;
    /// multi-arm chord capture is preserved by design but not yet wired
    /// through `CaptureSession` (M8 ensemble work).
    bool isArmed   { false };
    bool isFocused { false };
};

/// The Preparation tab's timeline view state: a strip per input, the Pills
/// laid across them, and the timeline span the strips share. The state is
/// playhead-independent — the timeline is structure, not gesture — so a
/// playhead overlay is the renderer's concern, not the selector's.
struct TimelineViewState
{
    /// The timeline's left and right edges, in LMC seconds. Drawn from
    /// the session's own duration through the root TempoMap.
    Rational startLmcSeconds;
    Rational endLmcSeconds;

    std::vector<TrackStripState> rows;
    std::vector<PillState>       pills;
};

/// Computes the timeline view from a session Constituent tree, a list of
/// `InputDescriptor`s describing the input topology, and the current
/// per-tape arm + focus state. Inputs without content still produce their
/// row (an empty track is still a real input).
///
/// The selector walks the tree the same way PerformanceViewState does —
/// accumulating conceptual position through nested TempoMaps — so the
/// timeline X-axis is always honest LMC seconds.
TimelineViewState selectTimelineView (const Constituent&                  root,
                                      const TempoMap&                     sessionToLmc,
                                      const std::vector<InputDescriptor>& inputs,
                                      const std::vector<TapeId>&          armedTapes,
                                      TapeId                              focusedTape);

} // namespace sirius
