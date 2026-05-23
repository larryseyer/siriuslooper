#include "ida/ClapBundleLoader.h"

#include <cstring>
#include <dlfcn.h>
#include <utility>

namespace ida
{

std::string ClapBundleLoader::resolveBinaryPath (std::string bundlePath)
{
   #ifdef __APPLE__
    while (! bundlePath.empty() && bundlePath.back() == '/')
        bundlePath.pop_back();
    const auto slash = bundlePath.find_last_of ('/');
    std::string basename = (slash == std::string::npos)
        ? bundlePath
        : bundlePath.substr (slash + 1);
    if (basename.size() >= 5
        && basename.compare (basename.size() - 5, 5, ".clap") == 0)
        basename.resize (basename.size() - 5);
    return bundlePath + "/Contents/MacOS/" + basename;
   #else
    return bundlePath;
   #endif
}

ClapBundleLoader ClapBundleLoader::load (const std::string& bundlePath,
                                         std::string&       outError)
{
    outError.clear();
    ClapBundleLoader loader;

    const auto binaryPath = resolveBinaryPath (bundlePath);

    loader.dlHandle_ = ::dlopen (binaryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (loader.dlHandle_ == nullptr)
    {
        const char* err = ::dlerror();
        outError = "dlopen failed: ";
        outError += (err != nullptr) ? err : "(no dlerror)";
        return loader;
    }

    auto* entry = static_cast<const clap_plugin_entry_t*> (
        ::dlsym (loader.dlHandle_, "clap_entry"));
    if (entry == nullptr || ! clap_version_is_compatible (entry->clap_version))
    {
        outError = "clap_entry missing or incompatible";
        loader.release();
        return loader;
    }

    if (! entry->init (binaryPath.c_str()))
    {
        outError = "entry->init returned false";
        loader.release();
        return loader;
    }
    loader.entry_ = entry;

    auto* factory = static_cast<const clap_plugin_factory_t*> (
        entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr || factory->get_plugin_count (factory) == 0)
    {
        outError = "no CLAP plug-in factory";
        loader.release();
        return loader;
    }
    loader.factory_ = factory;

    return loader;
}

ClapBundleLoader::~ClapBundleLoader()
{
    release();
}

ClapBundleLoader::ClapBundleLoader (ClapBundleLoader&& other) noexcept
    : dlHandle_ (other.dlHandle_)
    , entry_    (other.entry_)
    , factory_  (other.factory_)
{
    other.dlHandle_ = nullptr;
    other.entry_    = nullptr;
    other.factory_  = nullptr;
}

ClapBundleLoader& ClapBundleLoader::operator= (ClapBundleLoader&& other) noexcept
{
    if (this != &other)
    {
        release();
        dlHandle_       = other.dlHandle_;
        entry_          = other.entry_;
        factory_        = other.factory_;
        other.dlHandle_ = nullptr;
        other.entry_    = nullptr;
        other.factory_  = nullptr;
    }
    return *this;
}

void ClapBundleLoader::release() noexcept
{
    factory_ = nullptr;
    if (entry_ != nullptr)
    {
        entry_->deinit();
        entry_ = nullptr;
    }
    if (dlHandle_ != nullptr)
    {
        ::dlclose (dlHandle_);
        dlHandle_ = nullptr;
    }
}

std::vector<PluginDescriptor> ClapBundleLoader::descriptors (
    const std::string& bundlePath) const
{
    std::vector<PluginDescriptor> out;
    if (factory_ == nullptr) return out;

    const auto count = factory_->get_plugin_count (factory_);
    out.reserve (count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto* desc = factory_->get_plugin_descriptor (factory_, i);
        if (desc == nullptr || desc->id == nullptr) continue;

        PluginDescriptor d;
        d.format       = PluginFormat::Clap;
        d.uniqueId     = desc->id;
        d.version      = desc->version != nullptr ? desc->version : "";
        d.name         = desc->name    != nullptr ? desc->name    : "";
        d.manufacturer = desc->vendor  != nullptr ? desc->vendor  : "";
        d.filePath     = bundlePath;
        out.push_back (std::move (d));
    }
    return out;
}

const clap_plugin_t* ClapBundleLoader::createPlugin (
    const clap_host_t& host, const char* pluginId) const
{
    if (factory_ == nullptr || pluginId == nullptr) return nullptr;

    // Validate pluginId is exported before calling create_plugin —
    // some factories crash on unknown ids rather than returning nullptr.
    const auto count = factory_->get_plugin_count (factory_);
    bool found = false;
    for (std::uint32_t i = 0; i < count && ! found; ++i)
    {
        const auto* desc = factory_->get_plugin_descriptor (factory_, i);
        if (desc != nullptr && desc->id != nullptr
            && std::strcmp (desc->id, pluginId) == 0)
            found = true;
    }
    if (! found) return nullptr;

    return factory_->create_plugin (factory_, &host, pluginId);
}

} // namespace ida
