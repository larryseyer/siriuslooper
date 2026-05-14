#pragma once

#include "sirius/Constituent.h"
#include "sirius/Rational.h"
#include "sirius/TapeId.h"
#include "sirius/TempoMap.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sirius
{

/// One loop sounding at a queried moment: which loop, which tape, where to read,
/// and which repetition it is on.
struct ActiveRead
{
    ConstituentId loop;         ///< the loop Constituent that is sounding
    TapeId        tape;         ///< the tape it reads from
    Rational      tapePosition; ///< where in the tape to read, in LMC seconds
    std::int64_t  cycle;        ///< which repetition, 0-based — for mutation and cardinality
};

/// The outbound render pipeline (white paper Parts 3.6, 5.2): given a
/// Constituent tree and the session's tempo map to LMC time, it answers what
/// should sound at a queried LMC moment.
///
/// It walks the tree, composing each Constituent's local time domain with its
/// placement — this is the polymetric wiring: a child that carries its own
/// local tempo map runs in its own conceptual time domain, and the pipeline
/// composes that domain with every domain above it. For free-running leaf loops
/// it then computes the active tape read from the placement span, the
/// cardinality, and exact loop-cycle arithmetic.
///
/// M3 scope and honest limits: loops whose trigger is not FreeRunning are
/// correctly *dormant* — they await an external trigger the M3 pipeline does
/// not yet provide, so they genuinely should not sound. Until-conditions are
/// bounded by the placement span (their external evaluation context is a later
/// subsystem). Cross-referential phase (synchronized / phase-locked), the
/// mutation engine, and termination-as-stop-event are later subsystems; the
/// pipeline emits the `cycle` number that the mutation engine will consume.
class RenderPipeline
{
public:
    /// `root` is the outermost Constituent — a session or performance, with its
    /// own conceptual time starting at 0. `sessionToLmc` maps that conceptual
    /// time (in whole notes) to absolute LMC seconds. Throws std::invalid_argument
    /// if `root` is null.
    RenderPipeline (std::shared_ptr<const Constituent> root, TempoMap sessionToLmc);

    /// Every loop sounding at `lmcTime`, with its tape read position. The order
    /// of the result follows a depth-first walk of the tree.
    std::vector<ActiveRead> activeReadsAt (Rational lmcTime) const;

private:
    std::shared_ptr<const Constituent> root_;
    TempoMap sessionToLmc_;
};

} // namespace sirius
