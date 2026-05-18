#pragma once

#include "sirius/ChannelDefaults.h"
#include "sirius/InputKind.h"
#include "sirius/TapeId.h"

#include <optional>
#include <string>

namespace sirius
{

/// Light, free-standing metadata about a single input source. Pairs a
/// TapeId (the back-reference into the data layer) with the human-visible
/// shape of the input — what kind it is, what it is called, and (where
/// the kind has one) which channel or port index it is. Also carries the
/// initial-value flags an InputMixer copies into its runtime state on
/// registerInput (V7 alignment plan M3 — descriptor stays immutable value-
/// typed metadata; the mixer holds the mutable runtime state).
///
/// Honors the white paper §7.2 data-layer / structure-layer split:
/// Tape<T> is heavy, immutable data and does not know about descriptors;
/// InputDescriptor is light metadata that points *at* a tape by id.
///
/// `channelOrPortIndex` is intentionally optional — Transport and System
/// tapes are not indexed by channel or port.
struct InputDescriptor
{
    TapeId tapeId;
    InputKind inputKind;
    std::string displayName;
    std::optional<int> channelOrPortIndex;
    bool rawDirectMonitor { false };
    bool enabled { true };
    ChannelDefaults defaults {};
};

} // namespace sirius
