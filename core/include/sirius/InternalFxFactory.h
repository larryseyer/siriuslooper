#pragma once

#include "sirius/IInternalFxAdapter.h"
#include "sirius/InternalFxId.h"

#include <memory>

namespace sirius
{

/// Resolve an `InternalFxId` to a freshly-constructed adapter wrapping
/// the matching OTTO header-only Player FX.
///
/// T3a status (2026-05-23): `kEq` returns an `EqAdapter`. `kCmp`, `kRvb`,
/// and `kDly` return `nullptr` until T3b (CMP), T3c (DLY), and T3d (RVB)
/// land their adapters in subsequent sub-task sessions. `nullptr` is the
/// "no internal adapter — caller falls through to the OOP plugin path"
/// signal, mirroring `IEffectChainHost::pumpSlot`'s `false` return.
///
/// Allocates — message-thread only. Returned adapter is unprepared;
/// callers must invoke `prepare(sr, maxBlock)` before binding it to a
/// slot.
std::unique_ptr<IInternalFxAdapter> makeInternalFxAdapter (InternalFxId id);

} // namespace sirius
