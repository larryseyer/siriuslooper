#pragma once

#include "sirius/TapePool.h"
#include "sirius/InputMixer.h"

namespace sirius
{

/// Registers every pool tape that is not already a mixer tape terminal. The pool
/// is the source of truth; the mixer holds routing terminals. Idempotent. The
/// primary tape (TapeId{1}) is seeded by both ctors, so it is skipped here.
/// Message-thread only (mutates the mixer's tape-terminal registry).
inline void mirrorTapePool (const TapePool& pool, InputMixer& mixer)
{
    for (const auto& tape : pool.tapes())
        if (! mixer.hasTape (tape.id))
            mixer.addTape (tape.id);
}

} // namespace sirius
