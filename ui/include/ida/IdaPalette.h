#pragma once

#include "OTTOColours.h"

#include <cstddef>
#include <cstdint>

namespace ida::palette
{

/// IDA's colour method (sister-app parity with OTTO). The full rationale is
/// in docs/design/ida-colour-method.md; the short version:
///
///   * Every coloured entity draws from OTTO's eight player hues
///     (`otto::Colours::getPlayerColour`) — the shared sister-app palette.
///   * Colour is keyed off the entity's STABLE id, not its on-screen position,
///     so the same tape/phrase is the same colour everywhere (tree AND timeline)
///     and never shifts when rows reorder or the tree is edited.
///   * Loops take a SHADE of their parent phrase's hue, so a phrase and its loops
///     read as one colour family while still being distinguishable.

/// The base hue for a stable integer id: one of OTTO's eight player hues,
/// selected deterministically. Negative ids are folded into range.
inline juce::Colour hueForId (std::int64_t id) noexcept
{
    const int slot = static_cast<int> (((id % 8) + 8) % 8);
    return otto::Colours::getPlayerColour (slot);
}

/// A timeline tape (row): its own distinct hue, keyed by TapeId.
inline juce::Colour tapeColour (std::int64_t tapeId) noexcept { return hueForId (tapeId); }

/// A phrase: its own distinct base hue, keyed by ConstituentId. A phrase's Pill
/// on the timeline and its row in the structural tree share this colour.
inline juce::Colour phraseColour (std::int64_t phraseId) noexcept { return hueForId (phraseId); }

/// A loop within a phrase: a shade of the phrase's hue, stepped by the loop's
/// order among its siblings so adjacent loops differ slightly but stay in the
/// phrase's colour family. The step cycles so it never washes out to white/black.
inline juce::Colour loopShade (juce::Colour phraseHue, int loopOrderWithinPhrase) noexcept
{
    // Deltas chosen to stay legible on the dark (bg1) background: small,
    // alternating brighter/darker steps around the phrase hue.
    static constexpr float deltas[] = { 0.0f, 0.18f, -0.16f, 0.34f, -0.30f };
    const float d = deltas[static_cast<std::size_t> (((loopOrderWithinPhrase % 5) + 5) % 5)];
    return d >= 0.0f ? phraseHue.brighter (d) : phraseHue.darker (-d);
}

} // namespace ida::palette
