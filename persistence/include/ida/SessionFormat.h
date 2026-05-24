#pragma once

#include "ida/Constituent.h"
#include "ida/MixerGraphState.h"
#include "ida/TapePool.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ida::persistence
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

/// Serializes one mixer's routing-graph snapshot (routing-graph Phase 5) to a
/// self-contained JSON document. Independent of the Constituent session document
/// (the two mixers are separate consoles). Reuses the EffectChain serialization.
juce::String serializeMixerGraphState (const InputMixerGraphState&);
juce::String serializeMixerGraphState (const OutputMixerGraphState&);

/// Reconstructs a mixer snapshot from `serializeMixerGraphState`. Throws
/// std::runtime_error on malformed input. Absent optional keys default (an
/// empty graph), so a pre-graph document loads clean.
InputMixerGraphState  deserializeInputMixerGraphState  (const juce::String& json);
OutputMixerGraphState deserializeOutputMixerGraphState (const juce::String& json);

/// Serializes the project tape pool to a self-contained JSON document. Round-
/// trips exactly through deserializeTapePool. Independent of the Constituent
/// session document and the mixer-graph documents.
juce::String serializeTapePool (const TapePool& pool);

/// Reconstructs a tape pool from serializeTapePool's output. Throws
/// std::runtime_error on a malformed document. A present-but-empty tapes array
/// is rejected (the >=1 invariant is a load-time contract too). Callers loading
/// a pre-tape-pool session construct a default TapePool() instead of calling
/// this (forward-compat is the caller's responsibility, matching the mixer-graph
/// convention).
TapePool deserializeTapePool (const juce::String& json);

/// Serializes the OutputMixer's `ConstituentId -> OutputChannelId` phrase-
/// channel binding (slice P, 2026-05-24). The list lives outside the mixer
/// graph because the binding is owned by the MainComponent — the mixer only
/// sees raw OutputChannelIds. Round-trips exactly through
/// deserializePhraseChannelMap.
juce::String serializePhraseChannelMap (
    const std::vector<std::pair<std::int64_t, std::int64_t>>& entries);

/// Reconstructs a phrase-channel map. Throws std::runtime_error on malformed
/// input. A missing "entries" key is rejected — back-compat callers must
/// detect the envelope-level absence and skip the call (matches the
/// TapePool convention).
std::vector<std::pair<std::int64_t, std::int64_t>> deserializePhraseChannelMap (
    const juce::String& json);

} // namespace ida::persistence
