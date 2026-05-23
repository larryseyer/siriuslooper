#pragma once

namespace ida
{

/// Per-instance archival strategy for handling plug-in non-determinism
/// (white paper §15.6, §8.3). Default is `VersionPinning` — symbolic
/// fidelity is preserved unconditionally; the performer is warned about
/// plug-in version drift at session reopen and chooses how to react.
/// `DeterminismContract` and `WetCapture` land in later M8 sessions
/// (V7 plan lines 597-603).
enum class ArchivalMode
{
    DeterminismContract,
    WetCapture,
    VersionPinning,
};

} // namespace ida
