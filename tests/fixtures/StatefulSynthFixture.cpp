// =============================================================================
// StatefulSynthFixture — second synthetic CLAP for M8 S2 state-IPC tests.
// =============================================================================
// Shaped like a real third-party plug-in: distinct id + version, two
// parameters (cutoff Hz + resonance Q), per-instance internal state
// (a 1-pole low-pass coefficient recomputed from cutoff on activate +
// parameter change), and a real `clap_plugin_state` extension that
// serializes the parameters as a 16-byte fixed-layout payload.
//
// Multi-instance safe: per-`clap_plugin_t` state, no globals. Audio
// processing is a simple low-pass filter so the parameter changes
// produce audible (and assertable) output differences. MIT-licensed
// under Sirius — no third-party dependency.
//
// Built as a .clap bundle by tests/fixtures/CMakeLists.txt; tests find
// it via the SIRIUS_STATEFUL_SYNTH_CLAP_PATH generator-expression
// define.
// =============================================================================

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

constexpr const char* kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    nullptr
};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    "com.sirius.synthetic.statefulsynth",
    "Sirius Stateful Synth Fixture",
    "Sirius Looper Tests",
    "https://example.invalid/sirius",
    "https://example.invalid/sirius",
    "https://example.invalid/sirius",
    "1.0.0",
    "Stateful synth fixture for M8 S2 state-IPC round-trip tests.",
    kFeatures
};

constexpr std::uint8_t kStateMagic[4] = { 'S', 'L', 'S', 'T' };
constexpr std::uint32_t kStateVersion = 1u;

constexpr clap_id kParamCutoff    = 0;
constexpr clap_id kParamResonance = 1;
constexpr double  kDefaultCutoff  = 1000.0;
constexpr double  kDefaultReso    =    0.707;

struct PluginState
{
    clap_plugin_t      plugin {};
    const clap_host_t* host { nullptr };
    double             sampleRate { 48000.0 };
    double             cutoff     { kDefaultCutoff };
    double             resonance  { kDefaultReso };
    double             coeff      { 0.0 }; // recomputed on activate / param change
    double             z1Left     { 0.0 };
    double             z1Right    { 0.0 };
};

void recomputeCoefficient (PluginState& s)
{
    const double normalized = s.cutoff / (s.sampleRate * 0.5);
    const double clamped    = std::min (0.999, std::max (0.001, normalized));
    s.coeff = clamped;
}

// ---- audio-ports ---------------------------------------------------------
uint32_t audioPortsCount (const clap_plugin_t*, bool /*isInput*/) { return 1; }
bool audioPortsGet (const clap_plugin_t*, uint32_t index, bool isInput,
                    clap_audio_port_info_t* info)
{
    if (index != 0) return false;
    info->id = 0;
    std::snprintf (info->name, sizeof (info->name), "%s",
                   isInput ? "Input" : "Output");
    info->channel_count = 2;
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
constexpr clap_plugin_audio_ports_t kAudioPorts = { audioPortsCount, audioPortsGet };

// ---- params --------------------------------------------------------------
uint32_t paramsCount (const clap_plugin_t*) { return 2; }
bool paramsGetInfo (const clap_plugin_t*, uint32_t index,
                    clap_param_info_t* info)
{
    if (index == 0)
    {
        info->id          = kParamCutoff;
        info->flags       = CLAP_PARAM_IS_AUTOMATABLE;
        info->cookie      = nullptr;
        std::snprintf (info->name,   sizeof (info->name),   "Cutoff");
        std::snprintf (info->module, sizeof (info->module), "Filter");
        info->min_value     = 20.0;
        info->max_value     = 20000.0;
        info->default_value = kDefaultCutoff;
        return true;
    }
    if (index == 1)
    {
        info->id          = kParamResonance;
        info->flags       = CLAP_PARAM_IS_AUTOMATABLE;
        info->cookie      = nullptr;
        std::snprintf (info->name,   sizeof (info->name),   "Resonance");
        std::snprintf (info->module, sizeof (info->module), "Filter");
        info->min_value     = 0.5;
        info->max_value     = 10.0;
        info->default_value = kDefaultReso;
        return true;
    }
    return false;
}
bool paramsGetValue (const clap_plugin_t* plugin, clap_id paramId,
                     double* outValue)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    if (paramId == kParamCutoff)    { *outValue = s->cutoff;    return true; }
    if (paramId == kParamResonance) { *outValue = s->resonance; return true; }
    return false;
}
bool paramsValueToText (const clap_plugin_t*, clap_id, double value,
                        char* out, uint32_t cap)
{
    std::snprintf (out, cap, "%.3f", value);
    return true;
}
bool paramsTextToValue (const clap_plugin_t*, clap_id, const char* text,
                        double* outValue)
{
    *outValue = std::strtod (text, nullptr);
    return true;
}
void paramsFlush (const clap_plugin_t*, const clap_input_events_t*,
                  const clap_output_events_t*) {}
constexpr clap_plugin_params_t kParams = {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

// ---- state ---------------------------------------------------------------
bool stateSave (const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    std::uint8_t buf[16] {};
    std::memcpy (buf, kStateMagic, 4);
    const auto v = kStateVersion;
    std::memcpy (buf + 4, &v, 4);
    const float cutoffF = static_cast<float> (s->cutoff);
    const float resoF   = static_cast<float> (s->resonance);
    std::memcpy (buf + 8,  &cutoffF, 4);
    std::memcpy (buf + 12, &resoF,   4);
    const auto n = stream->write (stream, buf, sizeof (buf));
    return n == 16;
}
bool stateLoad (const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    std::uint8_t buf[16] {};
    const auto n = stream->read (stream, buf, sizeof (buf));
    if (n != 16) return false;
    if (std::memcmp (buf, kStateMagic, 4) != 0) return false;
    std::uint32_t version = 0;
    std::memcpy (&version, buf + 4, 4);
    if (version != kStateVersion) return false;
    float cutoffF = 0.0f, resoF = 0.0f;
    std::memcpy (&cutoffF, buf + 8,  4);
    std::memcpy (&resoF,   buf + 12, 4);
    s->cutoff    = cutoffF;
    s->resonance = resoF;
    recomputeCoefficient (*s);
    return true;
}
constexpr clap_plugin_state_t kState = { stateSave, stateLoad };

// ---- plug-in vtable ------------------------------------------------------
bool pluginInit (const clap_plugin_t* plugin)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    recomputeCoefficient (*s);
    return true;
}
void pluginDestroy (const clap_plugin_t* plugin)
{
    std::free (plugin->plugin_data);
}
bool pluginActivate (const clap_plugin_t* plugin, double sr, uint32_t, uint32_t)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    s->sampleRate = sr;
    recomputeCoefficient (*s);
    return true;
}
void pluginDeactivate    (const clap_plugin_t*) {}
bool pluginStartProcess  (const clap_plugin_t*) { return true; }
void pluginStopProcess   (const clap_plugin_t*) {}
void pluginReset         (const clap_plugin_t* plugin)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    s->z1Left  = 0.0;
    s->z1Right = 0.0;
}
void pluginOnMainThread  (const clap_plugin_t*) {}

clap_process_status pluginProcess (const clap_plugin_t* plugin,
                                   const clap_process_t* process)
{
    auto* s = static_cast<PluginState*> (plugin->plugin_data);
    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0)
        return CLAP_PROCESS_CONTINUE;
    const auto& in  = process->audio_inputs [0];
    const auto& out = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const double a = s->coeff;
    for (uint32_t i = 0; i < frames; ++i)
    {
        s->z1Left  += a * (static_cast<double> (in.data32[0][i]) - s->z1Left);
        s->z1Right += a * (static_cast<double> (in.data32[1][i]) - s->z1Right);
        out.data32[0][i] = static_cast<float> (s->z1Left);
        out.data32[1][i] = static_cast<float> (s->z1Right);
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension (const clap_plugin_t*, const char* id)
{
    if (std::strcmp (id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp (id, CLAP_EXT_PARAMS)      == 0) return &kParams;
    if (std::strcmp (id, CLAP_EXT_STATE)       == 0) return &kState;
    return nullptr;
}

const clap_plugin_t* pluginCreate (const clap_host_t* host)
{
    auto* state = static_cast<PluginState*> (std::calloc (1, sizeof (PluginState)));
    if (state == nullptr) return nullptr;
    state->host                    = host;
    state->cutoff                  = kDefaultCutoff;
    state->resonance               = kDefaultReso;
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

// ---- factory + entry ----------------------------------------------------
uint32_t factoryCount (const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryDescriptor (
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &kDescriptor : nullptr;
}
const clap_plugin_t* factoryCreate (const clap_plugin_factory_t*,
                                    const clap_host_t* host, const char* id)
{
    if (! clap_version_is_compatible (host->clap_version)) return nullptr;
    if (std::strcmp (id, kDescriptor.id) != 0) return nullptr;
    return pluginCreate (host);
}
constexpr clap_plugin_factory_t kFactory = {
    factoryCount, factoryDescriptor, factoryCreate
};

bool entryInit   (const char*) { return true; }
void entryDeinit () {}
const void* entryGetFactory (const char* id)
{
    if (std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0) return &kFactory;
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
