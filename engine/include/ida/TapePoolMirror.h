#pragma once

#include "ida/TapePool.h"
#include "ida/InputMixer.h"

namespace ida
{

/// Registers every pool tape that is not already a mixer tape terminal. The pool
/// is the source of truth; the mixer holds routing terminals. Idempotent: tapes
/// already present in the mixer are skipped via the hasTape guard, so calling
/// this more than once is safe and does not double-count. The pool's primary
/// tape is seeded by both the pool and mixer ctors, so it is always skipped on
/// the first call; subsequent calls also skip it (and every other present tape).
/// Message-thread only (mutates the mixer's tape-terminal registry).
inline void mirrorTapePool (const TapePool& pool, InputMixer& mixer)
{
    for (const auto& tape : pool.tapes())
        if (! mixer.hasTape (tape.id))
        {
            const bool ok = mixer.addTape (tape.id); // guarded by hasTape above — cannot fail
            (void) ok;
        }
}

} // namespace ida
