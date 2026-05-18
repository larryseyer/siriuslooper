// =============================================================================
// SyntheticTestPlugin — minimal CLAP plug-in used by M7 S2 round-trip tests.
// =============================================================================
// Identity-mode plug-in: copies stereo audio input to stereo audio output,
// byte-for-byte. No parameters, no note ports, no state. The deliberate-
// timeout + deliberate-crash variants land later — they belong with the M7
// watchdog (S4) and supervisor (S5) work, not here.
//
// Built as a CLAP bundle by tests/fixtures/CMakeLists.txt; the host binary
// (`sirius_plugin_host --mode clap --plugin-path <this>`) dlopens it and
// pumps audio through `process()`. Tests find it via the
// SIRIUS_SYNTHETIC_CLAP_PATH generator-expression define.
//
// Structure follows external/clap/src/plugin-template.c — kept deliberately
// close to the upstream reference so a CLAP-format change rebases cleanly.
// =============================================================================

#include <clap/clap.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

// -----------------------------------------------------------------------------
// Plug-in descriptor — single instance, stereo identity audio effect.
// -----------------------------------------------------------------------------
constexpr const char* kFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    "com.sirius.synthetic.identity",
    "Sirius Synthetic Identity",
    "Sirius Looper Tests",
    "https://example.invalid/sirius",
    "https://example.invalid/sirius",
    "https://example.invalid/sirius",
    "1.0.0",
    "Identity audio effect used by Sirius Looper round-trip tests.",
    kFeatures
};

struct PluginState
{
    clap_plugin_t      plugin {};
    const clap_host_t* host { nullptr };
};

// -----------------------------------------------------------------------------
// AUDIO_PORTS extension — 1 stereo input + 1 stereo output.
// -----------------------------------------------------------------------------
uint32_t audioPortsCount (const clap_plugin_t*, bool /*isInput*/) { return 1; }

bool audioPortsGet (const clap_plugin_t*,
                    uint32_t                index,
                    bool                    isInput,
                    clap_audio_port_info_t* info)
{
    if (index != 0)
        return false;
    info->id            = 0;
    std::snprintf (info->name, sizeof (info->name), "%s", isInput ? "Input" : "Output");
    info->channel_count = 2;
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

constexpr clap_plugin_audio_ports_t kAudioPorts = {
    audioPortsCount,
    audioPortsGet
};

// -----------------------------------------------------------------------------
// Plug-in vtable
// -----------------------------------------------------------------------------
bool pluginInit          (const clap_plugin_t*) { return true; }
void pluginDestroy       (const clap_plugin_t* plugin)
{
    std::free (plugin->plugin_data);
}
bool pluginActivate      (const clap_plugin_t*, double, uint32_t, uint32_t) { return true; }
void pluginDeactivate    (const clap_plugin_t*) {}
bool pluginStartProcess  (const clap_plugin_t*) { return true; }
void pluginStopProcess   (const clap_plugin_t*) {}
void pluginReset         (const clap_plugin_t*) {}
void pluginOnMainThread  (const clap_plugin_t*) {}

clap_process_status pluginProcess (const clap_plugin_t* /*plugin*/,
                                   const clap_process_t* process)
{
    const uint32_t frames = process->frames_count;
    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0)
        return CLAP_PROCESS_CONTINUE;

    const auto& in  = process->audio_inputs [0];
    const auto& out = process->audio_outputs[0];

    const uint32_t channels = in.channel_count < out.channel_count
                            ? in.channel_count
                            : out.channel_count;

    // Identity: byte-for-byte copy of each channel buffer.
    for (uint32_t ch = 0; ch < channels; ++ch)
        std::memcpy (out.data32[ch], in.data32[ch], frames * sizeof (float));

    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension (const clap_plugin_t*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &kAudioPorts;
    return nullptr;
}

const clap_plugin_t* pluginCreate (const clap_host_t* host)
{
    auto* state = static_cast<PluginState*> (std::calloc (1, sizeof (PluginState)));
    if (state == nullptr)
        return nullptr;
    state->host                    = host;
    state->plugin.desc             = &kDescriptor;
    state->plugin.plugin_data      = state;
    state->plugin.init             = pluginInit;
    state->plugin.destroy          = pluginDestroy;
    state->plugin.activate         = pluginActivate;
    state->plugin.deactivate       = pluginDeactivate;
    state->plugin.start_processing = pluginStartProcess;
    state->plugin.stop_processing  = pluginStopProcess;
    state->plugin.reset            = pluginReset;
    state->plugin.process          = pluginProcess;
    state->plugin.get_extension    = pluginGetExtension;
    state->plugin.on_main_thread   = pluginOnMainThread;
    return &state->plugin;
}

// -----------------------------------------------------------------------------
// Plug-in factory
// -----------------------------------------------------------------------------
uint32_t factoryCount (const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* factoryDescriptor (const clap_plugin_factory_t*,
                                                   uint32_t index)
{
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* factoryCreate (const clap_plugin_factory_t*,
                                    const clap_host_t*           host,
                                    const char*                  pluginId)
{
    if (! clap_version_is_compatible (host->clap_version))
        return nullptr;
    if (std::strcmp (pluginId, kDescriptor.id) != 0)
        return nullptr;
    return pluginCreate (host);
}

constexpr clap_plugin_factory_t kFactory = {
    factoryCount,
    factoryDescriptor,
    factoryCreate
};

// -----------------------------------------------------------------------------
// Entry-point
// -----------------------------------------------------------------------------
bool entryInit   (const char* /*pluginPath*/) { return true; }
void entryDeinit (void) {}

const void* entryGetFactory (const char* factoryId)
{
    if (std::strcmp (factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &kFactory;
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
