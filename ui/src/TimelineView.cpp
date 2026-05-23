#include "ida/TimelineView.h"

#include "ida/IdaPalette.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace sirius
{

namespace
{
    const char* kindGlyph (InputKind k) noexcept
    {
        switch (k)
        {
            case InputKind::Audio:               return "AUD";
            case InputKind::Video:               return "VID";
            case InputKind::Midi:                return "MID";
            case InputKind::Control:             return "CTL";
            case InputKind::ParameterAutomation: return "PAR";
            case InputKind::Transport:           return "TRN";
            case InputKind::System:              return "SYS";
        }
        return "?";
    }


    int findRowIndexForTape (const TimelineViewState& s, TapeId t)
    {
        for (std::size_t i = 0; i < s.rows.size(); ++i)
            if (s.rows[i].tapeId == t)
                return static_cast<int> (i);
        return -1;
    }
}

void TimelineView::setState (TimelineViewState newState)
{
    state_ = std::move (newState);
    repaint();
}

void TimelineView::setPlayhead (std::optional<Rational> lmcSeconds)
{
    playhead_ = lmcSeconds;
    repaint();
}

int TimelineView::totalHeight() const
{
    return rulerHeight + static_cast<int> (state_.rows.size()) * rowHeight;
}

juce::Rectangle<int> TimelineView::stripBounds (int rowIndex) const
{
    return { 0, rulerHeight + rowIndex * rowHeight, stripColumnWidth, rowHeight };
}

juce::Rectangle<int> TimelineView::armHitBox (int rowIndex) const
{
    // Right-aligned inside the strip column so it sits closest to the
    // content area — the eye reads "this arm targets this row's content".
    auto strip = stripBounds (rowIndex);
    return { strip.getRight() - 56, strip.getY() + 16, 48, 20 };
}

juce::Rectangle<int> TimelineView::contentArea (int rowIndex) const
{
    return { stripColumnWidth, rulerHeight + rowIndex * rowHeight,
             std::max (0, getWidth() - stripColumnWidth), rowHeight };
}

int TimelineView::timeToX (Rational t) const
{
    const auto contentWidth = std::max (0, getWidth() - stripColumnWidth);
    const auto span = state_.endLmcSeconds - state_.startLmcSeconds;
    if (! (span > Rational (0)) || contentWidth == 0)
        return stripColumnWidth;
    const double frac = ((t - state_.startLmcSeconds).toDouble())
                      / (span.toDouble());
    return stripColumnWidth
         + static_cast<int> (std::round (frac * contentWidth));
}

void TimelineView::paint (juce::Graphics& g)
{
    const auto bg = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    g.fillAll (bg);

    // --- Time ruler ---
    {
        juce::Rectangle<int> ruler { 0, 0, getWidth(), rulerHeight };
        g.setColour (bg.brighter (0.08f));
        g.fillRect (ruler);

        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                      11.0f, 0));
        const auto span = state_.endLmcSeconds - state_.startLmcSeconds;
        if (span > Rational (0))
        {
            // Honest tick spacing: every two LMC seconds. Coarse on purpose —
            // performance-mode timelines aren't measurement instruments.
            const double endSec = state_.endLmcSeconds.toDouble();
            for (double s = state_.startLmcSeconds.toDouble();
                 s <= endSec + 1e-6; s += 2.0)
            {
                const int x = timeToX (Rational (static_cast<std::int64_t> (s * 1000),
                                                 1000));
                g.drawVerticalLine (x, 4.0f, rulerHeight - 2.0f);
                g.drawText (juce::String ((int) s) + "s",
                            x + 2, 2, 40, rulerHeight - 4,
                            juce::Justification::topLeft, false);
            }
        }
        g.setColour (bg.darker (0.3f));
        g.drawHorizontalLine (rulerHeight - 1, 0.0f, (float) getWidth());
    }

    // --- Track strips ---
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                  12.0f, 0));
    for (std::size_t i = 0; i < state_.rows.size(); ++i)
    {
        const auto& row     = state_.rows[i];
        const int   ri      = static_cast<int> (i);
        const auto  strip   = stripBounds (ri);
        const auto  content = contentArea (ri);

        // Row backgrounds: alternating bands so dense rows stay legible.
        const auto rowTint = (i % 2 == 0) ? bg.brighter (0.02f)
                                          : bg.brighter (0.06f);
        g.setColour (rowTint);
        g.fillRect (strip);
        g.fillRect (content);
        g.setColour (bg.darker (0.3f));
        g.drawHorizontalLine (strip.getBottom() - 1,
                              0.0f, (float) getWidth());

        // Per-tape colour band on the left edge of the strip — each tape gets
        // its own OTTO hue (keyed by TapeId, stable across reorders). This is
        // the glance affordance that ties a row to its pills.
        g.setColour (palette::tapeColour (row.tapeId.value()));
        g.fillRect (strip.getX(), strip.getY() + 2,
                    4, strip.getHeight() - 4);

        // Focused row gets a brighter left edge so the bottom bar's Mark In
        // gesture has a clear referent.
        if (row.isFocused)
        {
            g.setColour (juce::Colours::white);
            g.fillRect (strip.getX() + 4, strip.getY() + 2,
                        2, strip.getHeight() - 4);
        }

        // Kind glyph + display name.
        g.setColour (juce::Colours::white);
        g.drawText ("[" + juce::String (kindGlyph (row.kind)) + "]",
                    strip.getX() + 12, strip.getY() + 4,
                    36, 18, juce::Justification::centredLeft, false);
        g.drawText (juce::String (row.displayName),
                    strip.getX() + 12, strip.getY() + 22,
                    strip.getWidth() - 80, 18,
                    juce::Justification::centredLeft, false);

        // Arm button (own rectangle so it can be hit-tested in mouseDown).
        const auto arm = armHitBox (ri);
        g.setColour (row.isArmed ? juce::Colours::darkred
                                 : juce::Colours::darkgrey);
        g.fillRoundedRectangle (arm.toFloat(), 4.0f);
        g.setColour (juce::Colours::white);
        g.drawText (row.isArmed ? juce::String ("Disarm") : juce::String ("Arm"),
                    arm, juce::Justification::centred, false);

        // Content area boundary on the left edge.
        g.setColour (bg.darker (0.4f));
        g.drawVerticalLine (stripColumnWidth, (float) strip.getY(),
                            (float) strip.getBottom());
    }

    // --- Pills ---
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                  11.0f, 0));
    for (const auto& pill : state_.pills)
    {
        const int primaryIdx = findRowIndexForTape (state_, pill.primaryTape);
        if (primaryIdx < 0)
            continue;

        const auto content = contentArea (primaryIdx);
        const int x1 = timeToX (pill.startLmcSeconds);
        const int x2 = timeToX (pill.endLmcSeconds);
        juce::Rectangle<int> pillRect { x1 + 1,
                                        content.getY() + 4,
                                        std::max (10, x2 - x1 - 2),
                                        content.getHeight() - 8 };

        // Each Pill carries its phrase's hue (keyed by the phrase ConstituentId,
        // so the same phrase is this colour in the tree too). OTTO pill treatment:
        // a dark fill of the hue with the bright hue as the border.
        const auto hue  = palette::phraseColour (pill.id.value());
        const auto fill = hue.darker (0.55f);
        g.setColour (fill);
        g.fillRoundedRectangle (pillRect.toFloat(), 8.0f);
        g.setColour (hue);
        g.drawRoundedRectangle (pillRect.toFloat(), 8.0f, 1.5f);

        // OTTO 4-corner contract — top-left loop count, top-right ↻ toggle,
        // bottom-left entrance, bottom-right exit, name in the middle.
        const int pad = 6;
        const auto inner = pillRect.reduced (pad, pad);

        g.setColour (juce::Colours::white);
        g.drawText (juce::String (pill.loopCount) + " loop"
                        + (pill.loopCount == 1 ? juce::String() : juce::String ("s")),
                    inner.getX(), inner.getY(),
                    inner.getWidth() / 2, 14,
                    juce::Justification::topLeft, false);

        g.drawText (pill.phraseLoopActive ? juce::String ("loop on")
                                          : juce::String ("once"),
                    inner.getX() + inner.getWidth() / 2, inner.getY(),
                    inner.getWidth() / 2, 14,
                    juce::Justification::topRight, false);

        g.drawText (juce::String (pill.entranceName),
                    inner.getX(), inner.getBottom() - 14,
                    inner.getWidth() / 2, 14,
                    juce::Justification::bottomLeft, false);
        g.drawText (juce::String (pill.exitName),
                    inner.getX() + inner.getWidth() / 2, inner.getBottom() - 14,
                    inner.getWidth() / 2, 14,
                    juce::Justification::bottomRight, false);

        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                      13.0f, juce::Font::bold));
        g.drawText (juce::String (pill.name),
                    pillRect, juce::Justification::centred, true);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                      11.0f, 0));

        if (pill.hasOverlays)
        {
            // Small filled circle at the pill's top-right corner — "something
            // extra lives here on this one only." No text; the dot is the
            // signal, matched to the white paper's glanceable principle.
            const int dotR = 3;
            g.setColour (juce::Colours::orange.withAlpha (0.9f));
            g.fillEllipse (juce::Rectangle<float> (
                static_cast<float> (x2 - dotR * 2 - 4),
                static_cast<float> (pillRect.getY() + 4),
                static_cast<float> (dotR * 2),
                static_cast<float> (dotR * 2)));
        }

        if (pill.isForked)
        {
            // Prime mark — a small upright tick above the pill, signaling
            // "this one is its own thing now." Distinct from the tie-bar
            // (horizontal) and the overlay dot (round) so the three marks
            // read independently at a glance.
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                          14.0f, juce::Font::bold));
            g.drawText ("'",
                        juce::Rectangle<int> (x2 - 14, pillRect.getY() - 14, 12, 14),
                        juce::Justification::centred, false);
            g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                          11.0f, 0));
        }

        // Membership outline — drawn on secondary rows the Pill claims, so
        // a multi-tape phrase is visibly honest even though its atoms only
        // render on the primary row.
        for (const auto& tape : pill.memberTapes)
        {
            if (tape == pill.primaryTape)
                continue;
            const int idx = findRowIndexForTape (state_, tape);
            if (idx < 0)
                continue;
            const auto secondary = contentArea (idx);
            juce::Rectangle<int> outlineRect { x1 + 1,
                                               secondary.getY() + 8,
                                               std::max (10, x2 - x1 - 2),
                                               secondary.getHeight() - 16 };
            g.setColour (fill.brighter (0.4f));
            g.drawRoundedRectangle (outlineRect.toFloat(), 6.0f, 1.0f);
        }
    }

    // Tie-bar: a thin horizontal mark across the top of each group of shared
    // placement wrappers. Surfaces "these are the same phrase" visually so the
    // operator's mental model is shape-and-position, not text. Drawn once per
    // group, spanning the leftmost shared pill's start to the rightmost's end.
    std::unordered_map<std::int64_t, std::vector<const PillState*>> tieGroups;
    for (const auto& pill : state_.pills)
        if (! pill.sharedSiblings.empty())
        {
            // Group key: minimum id in the group (own id and siblings). The
            // smallest id stays stable across paint frames so the same set
            // always coalesces under one entry.
            std::int64_t key = pill.id.value();
            for (const auto& s : pill.sharedSiblings)
                if (s.value() < key) key = s.value();
            tieGroups[key].push_back (&pill);
        }

    g.setColour (juce::Colours::lightblue.withAlpha (0.75f));
    for (const auto& [key, members] : tieGroups)
    {
        juce::ignoreUnused (key);
        if (members.size() < 2) continue;
        int leftX  = std::numeric_limits<int>::max();
        int rightX = std::numeric_limits<int>::min();
        int topY   = std::numeric_limits<int>::max();
        for (const auto* p : members)
        {
            leftX  = std::min (leftX,  timeToX (p->startLmcSeconds));
            rightX = std::max (rightX, timeToX (p->endLmcSeconds));
            const int idx = findRowIndexForTape (state_, p->primaryTape);
            if (idx >= 0)
                topY = std::min (topY, contentArea (idx).getY() + 4);
        }
        if (topY == std::numeric_limits<int>::max())
            continue;
        // Sit 6 px above the top edge of the (highest) pill in the group.
        g.fillRect (juce::Rectangle<int> (leftX, topY - 6, rightX - leftX, 2));
    }

    // --- Playhead overlay ---
    // Drawn last so it sits on top of the rows + pills, matching DAW
    // convention (Reaper, Pro Tools). Suppressed when the playhead is
    // outside the visible LMC span or no playhead is set at all.
    if (playhead_.has_value())
    {
        const auto p = *playhead_;
        if (p >= state_.startLmcSeconds && p <= state_.endLmcSeconds)
        {
            const int x = timeToX (p);
            const int top    = rulerHeight;
            const int bottom = totalHeight();

            g.setColour (juce::Colour (0xffffd24a));
            g.fillRect (juce::Rectangle<int> { x - 1, top, 2, bottom - top });

            // A small downward chevron in the ruler band makes the head of
            // the playhead readable as a "this is where the gesture lands"
            // marker rather than a generic vertical guide.
            juce::Path head;
            head.addTriangle ((float) x - 5.0f, 2.0f,
                              (float) x + 5.0f, 2.0f,
                              (float) x,        (float) (rulerHeight - 2));
            g.fillPath (head);
        }
    }
}

void TimelineView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.getY() < rulerHeight)
        return;

    // Context gesture (right-click / ctrl-click / touch long-press) on a Pill
    // opens the per-Pill command popup. Tested before arm/focus so the
    // gesture isn't eaten by the row-level handlers. The geometry mirrors the
    // paint loop's per-pill rectangle exactly so what looks like a Pill is.
    const bool isContextGesture = e.mods.isRightButtonDown()
                                || e.mods.isCtrlDown()
                                || e.source.isLongPressOrDrag();
    if (isContextGesture)
    {
        for (const auto& pill : state_.pills)
        {
            const int primaryIdx = findRowIndexForTape (state_, pill.primaryTape);
            if (primaryIdx < 0) continue;
            const auto content = contentArea (primaryIdx);
            const int x1 = timeToX (pill.startLmcSeconds);
            const int x2 = timeToX (pill.endLmcSeconds);
            const juce::Rectangle<int> pillRect { x1 + 1,
                                                  content.getY() + 4,
                                                  std::max (10, x2 - x1 - 2),
                                                  content.getHeight() - 8 };
            if (pillRect.contains (p))
            {
                if (onPillContextMenuRequested)
                    onPillContextMenuRequested (pill.id);
                return;
            }
        }
        // Context gesture landed in empty space — fall through so a
        // right-click on a strip's arm region still toggles arm (matches
        // existing DAW convention; the row-level handlers are gesture-
        // agnostic and don't care which mouse button fired).
    }

    const int rowIndex = (p.getY() - rulerHeight) / rowHeight;
    if (rowIndex < 0 || rowIndex >= (int) state_.rows.size())
        return;

    const auto& row = state_.rows[static_cast<std::size_t> (rowIndex)];

    if (armHitBox (rowIndex).contains (p))
    {
        if (onArmClicked) onArmClicked (row.tapeId);
        return;
    }

    if (onFocusClicked) onFocusClicked (row.tapeId);
}

} // namespace sirius
