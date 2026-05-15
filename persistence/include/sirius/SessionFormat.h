#pragma once

#include "sirius/Constituent.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace sirius::persistence
{

/// Serializes a Constituent graph — the entire structure layer of a session —
/// to JSON (white paper Part 7.8: "serializes to kilobytes of JSON"). The
/// output is a complete document carrying every Constituent's id, conceptual
/// boundaries, local meter and tempo map, anchor, name, repetition rules,
/// phrase metadata, tape references, and children, in a form intended to be
/// stable across sessions and safe to diff. The data layer — tape files — is
/// persisted separately by TapeStore.
juce::String serializeSession (const Constituent& root);

/// Reconstructs a Constituent graph from a session JSON document produced by
/// `serializeSession`. Throws std::runtime_error if the document is malformed
/// or references an unknown variant tag — degradation is announced, not silent
/// (white paper Part 13.3, rule 3); a corrupt session is a hard error.
std::shared_ptr<const Constituent> deserializeSession (const juce::String& json);

} // namespace sirius::persistence
