#pragma once

#include "sirius/Constituent.h"
#include "sirius/Rational.h"
#include "sirius/TempoMap.h"

#include <memory>

namespace sirius
{

/// A demo Constituent tree plus the tempo map that places it in LMC time.
/// This is the M3 app's stand-in for a loaded session: it exists so the UI has
/// a real Constituent hierarchy and a real RenderPipeline to exercise, ahead of
/// persistence (M4) and audio-device wiring (the operator-deferred half of M2).
struct DemoSession
{
    std::shared_ptr<const Constituent> root;
    TempoMap sessionToLmc;
    Rational lengthLmcSeconds; ///< the session's full span in LMC seconds
};

/// Builds the demo session: a sequence of three named phrases under a session
/// Constituent, the middle phrase itself a layer of two simultaneous loops.
/// Every leaf loop carries a TapeReference, so the RenderPipeline reports real
/// active reads as the UI scrubs through time.
DemoSession buildDemoSession();

} // namespace sirius
