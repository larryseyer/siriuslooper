#pragma once

#include "ida/PluginDescriptor.h"

#include <clap/clap.h>

#include <string>
#include <vector>

namespace ida
{

/// RAII loader for one .clap bundle (M8 S2). Construct via `load(...)`
/// → dlopen + `clap_entry` resolution + version-compat check + entry init
/// + factory acquisition. Destruct → entry deinit + dlclose, in that
/// strict order. Move-only because the underlying `void*` dlopen handle
/// and `clap_plugin_entry_t*` must not be duplicated.
///
/// Used by both `host/src/ClapScanner.cpp` (which walks every CLAP
/// bundle on disk and harvests descriptors) and `host_process/main.cpp`
/// (which loads the one bundle the engine asked for and pumps audio
/// through it). The two callers share the same load sequence; this
/// class is its single source of truth.
///
/// Threading: construction + destruction are **message-thread only** —
/// `dlopen` allocates and `entry->init` may take seconds for heavyweight
/// plug-ins. `descriptors()` and `createPlugin()` are const and may be
/// called from any thread that holds a non-mutating reference, though
/// in practice they too are message-thread because their callers are.
class ClapBundleLoader
{
public:
    /// Loads the bundle at `bundlePath`. On macOS this is the `*.clap`
    /// bundle directory; the loader resolves the inner binary at
    /// `<bundlePath>/Contents/MacOS/<basename-without-.clap>`. On
    /// Linux/Windows `bundlePath` is the shared library directly.
    ///
    /// `outError` receives a human-readable failure string on any
    /// load-step failure; the returned loader is `!valid()` in that
    /// case. The caller surfaces `outError` through `PluginScanResult::
    /// failedFiles` (scanner path) or `stderr` (child-process path).
    static ClapBundleLoader load (const std::string& bundlePath,
                                  std::string&       outError);

    ~ClapBundleLoader();
    ClapBundleLoader (ClapBundleLoader&&) noexcept;
    ClapBundleLoader& operator= (ClapBundleLoader&&) noexcept;
    ClapBundleLoader (const ClapBundleLoader&)            = delete;
    ClapBundleLoader& operator= (const ClapBundleLoader&) = delete;

    /// True iff the bundle was loaded, the entry was found + initialized,
    /// and the factory was acquired. False if any step failed; in that
    /// case all later methods return empty / nullptr.
    bool valid() const noexcept { return factory_ != nullptr; }

    const clap_plugin_entry_t*   entry()   const noexcept { return entry_; }
    const clap_plugin_factory_t* factory() const noexcept { return factory_; }

    /// Walks the factory and returns one `PluginDescriptor` per plug-in
    /// the bundle exports. Most bundles export exactly one; shells
    /// (Surge XT exports a synth + several effects, e.g.) export
    /// several. Populated fields: format = Clap, filePath = bundlePath,
    /// uniqueId = descriptor->id, version = descriptor->version,
    /// name = descriptor->name, manufacturer = descriptor->vendor.
    /// Returns empty if `!valid()`.
    std::vector<PluginDescriptor> descriptors (const std::string& bundlePath) const;

    /// Instantiates the plug-in identified by `pluginId` against `host`.
    /// Returned pointer is owned by the bundle's entry — caller must NOT
    /// `delete`; CLAP's `destroy` callback is the disposal path. Returns
    /// nullptr if `!valid()` or `pluginId` is not exported by this bundle.
    const clap_plugin_t* createPlugin (const clap_host_t& host,
                                       const char*        pluginId) const;

    /// On macOS, resolves the inner binary path. Static + public so
    /// other code (notably the scanner's "is this a CLAP bundle" check)
    /// can use it without duplicating the logic.
    static std::string resolveBinaryPath (std::string bundlePath);

private:
    ClapBundleLoader() = default;
    void release() noexcept;

    void*                        dlHandle_ { nullptr };
    const clap_plugin_entry_t*   entry_    { nullptr };
    const clap_plugin_factory_t* factory_  { nullptr };
};

} // namespace ida
