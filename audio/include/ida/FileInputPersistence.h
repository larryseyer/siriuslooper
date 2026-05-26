#pragma once

#include <juce_core/juce_core.h>

namespace ida
{
class FileInputRegistry;

/// Writes the registry's full state to a JSON object containing a single
/// "fileInputs" array. Schema documented in spec §4.7. Lossless for the
/// persisted subset (displayName, loopScope, windowOpacity, entry paths);
/// transient state (transport, missing flags, lazy durations) is dropped.
/// Returns a `juce::var` (not a serialized string like `serializeTapePool`)
/// so the caller can compose this directly into a parent Input Mixer JSON
/// object as the `fileInputs` property.
juce::var serializeFileInputs (const FileInputRegistry& registry);

/// Reads `root`'s "fileInputs" array (if present) and registers each entry
/// into `registry`. Returns true on success — including the case where
/// "fileInputs" is absent (backward compat for sessions predating Task 9).
/// `windowOpacity` is clamped to [0.5, 1.0]; malformed loopScope falls back
/// to "off". Entry `path` is required; entry `entryId` is informational
/// and discarded (registry allocates fresh handles).
bool deserializeFileInputs (FileInputRegistry& registry, const juce::var& root);

} // namespace ida
