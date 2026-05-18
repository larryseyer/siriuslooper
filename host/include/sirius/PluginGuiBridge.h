#pragma once

#include "sirius/IGuiBridge.h"

namespace sirius
{

/// Process-singleton accessor for the engine-side GUI bridge port.
///
/// First `instance()` call lazily constructs the concrete
/// `PluginGuiBridgeImpl` (which opens the XPC connection to the
/// bundled `sirius_gui_bridge` service). Tests bypass this via
/// `setInstanceForTesting` + `resetForTesting`.
///
/// **Lifetime.** The concrete impl is a function-local static — it
/// lives until process exit. The XPC connection is held for the same
/// duration so the kernel doesn't reclaim the registered send-right
/// before children get to ask for it.
class PluginGuiBridge
{
public:
    /// Returns the current bridge. Defaults to a NullGuiBridge that
    /// reports `isReady() == false`; first real-use construction
    /// happens inside `host/src/PluginGuiBridge.cpp` on Apple.
    ///
    /// The returned reference is valid for the full process lifetime.
    /// Callers may cache it; tests that toggle inject/uninject must
    /// re-call `instance()` after each toggle rather than caching
    /// across `setInstanceForTesting` boundaries.
    static IGuiBridge& instance() noexcept;

    /// Test-only: replace the active bridge with the given stub.
    /// `nullptr` reverts to NullGuiBridge. Caller owns the lifetime
    /// of `bridge`.
    static void setInstanceForTesting (IGuiBridge* bridge) noexcept;

    /// Test-only: clears the injected bridge so the next `instance()`
    /// returns the default (NullGuiBridge in Task 1, real impl in
    /// Task 3+).
    static void resetForTesting() noexcept;
};

} // namespace sirius
