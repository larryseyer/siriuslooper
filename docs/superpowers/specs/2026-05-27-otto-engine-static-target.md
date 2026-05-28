# OTTOEngine — a sixth OTTO build shape for IDA embedding

**Date:** 2026-05-27
**Status:** APPROVED (operator-confirmed 2026-05-27)
**Supersedes (partially):** `2026-05-27-otto-integration-architecture.md` §1.2 commitment 2 keeps its intent; this spec changes the engineering realization. The S1 plan `plans/2026-05-27-otto-host-embeds-ottoprocessor.md` is **invalidated** by this decision and must be re-written before re-execution.

---

## Why this document exists

The 2026-05-27 OTTO integration architecture spec said "OttoHost embeds full `OTTOProcessor`." A subsequent S1 implementation plan walked through the engineering with five tasks. Mid-execution the plan hit a CMake-integration wall:

- OTTO is consumed by IDA as a git submodule. IDA's `cmake/Dependencies.cmake:233` calls only `add_subdirectory("${OTTO_CORE_PATH}" ...)` — pulling in *otto-core*, not *otto-plugin*.
- `OTTOProcessor` is defined in `external/OTTO/src/otto-plugin/PluginProcessor.h`, compiled into a JUCE plugin target named `OTTOPlugin` that IDA's build never sees.
- Adding `add_subdirectory(otto-plugin)` to IDA's CMake would require setting ~15 OTTO-top-level cache variables (`OTTO_PRODUCT_NAME`, `OTTO_BUNDLE_ID`, `OTTO_COMPANY_NAME`, `OTTO_PLUGIN_MANUFACTURER_CODE`, `OTTO_PLATFORM_MACOS`, `OTTO_IS_SYNTH`, …) plus repointing `${CMAKE_SOURCE_DIR}`-relative includes in `SharedPluginSources.cmake` and would drag in plugin-format bundle generation (VST3 / AU / CLAP / Standalone .app) that IDA does not want.

The plan did not anticipate this. Future Claude sessions repeating the same plan-without-verification would hit the same wall. This document captures the *why* of the decision, the rejected alternatives, and the locked path forward — so the next session can read this once and move correctly.

This document is **architecture, not implementation.** The line-by-line edits live in the (to-be-written) implementation plan.

---

## 1. What IDA actually needs from OTTO

The architectural goal locked at the 2026-05-27 brainstorm (see `project_otto_integration_locked_decisions` + whitepaper V10 §5.7):

- IDA owns a single, embedded instance of OTTO's full audio engine.
- That instance runs OTTO's Conductor + Pattern engine + MIDI dispatch + sampler voices + internal FX + GlobalMixer — i.e. everything that lives behind `OTTOProcessor::processBlock`.
- That instance's transport is **self-driven** (OTTO's internal `TransportTracker` is authoritative; IDA has no engine-side transport — `project_otto_is_the_transport_source`).
- IDA reads the 32 stereo per-output buffers OTTO produces inside GlobalMixer.
- IDA hosts OTTO's existing `PluginEditor` inside an IDA tab via `OTTOProcessor::createEditor()`.
- IDA does NOT compile a separate copy of OTTO's code, port any OTTO logic into IDA, or maintain an IDA-side fork of any OTTO subsystem. Single source of truth lives in the OTTO submodule.

These constraints are inflexible. They follow from the locked decisions and the whitepaper. Any S1 design that violates one of them is wrong on its face.

---

## 2. OTTO's five existing build shapes, and why none of them fit

OTTO already ships in five shapes. Each is configured by `juce_add_plugin(OTTOPlugin ...)` plus its format-specific wrapper. Here is the inventory:

| Shape | CMake target(s) | Transport authority | What it produces on disk | Why it doesn't fit IDA |
|---|---|---|---|---|
| **Standalone** | `OTTOPlugin_Standalone` | OTTO's internal `TransportTracker` (driven by `juce::StandaloneFilterApp`) | A native `.app` / `.exe` / Linux binary | It IS the app. IDA cannot link an `add_executable` target as a library. |
| **VST3** | `OTTOPlugin_VST3` | Host (DAW) via `juce::AudioPlayHead` | `.vst3` bundle | Transport is slaved to the host. IDA has no transport to feed. Format SDK pulled in. |
| **AU** | `OTTOPlugin_AU` | Host via `juce::AudioPlayHead` | `.component` bundle | Same as VST3, plus Apple AU API. |
| **CLAP** | `OTTOPlugin_CLAP` | Host via the CLAP transport extension | `.clap` bundle | Same slave-to-host pattern. |
| **AUv3 (iOS)** | `OTTOPlugin_AUv3` | iOS AU host via `juce::AudioPlayHead` | App-extension `.appex` inside an iOS bundle | Slave-to-host. On iOS, IDA itself is an AUv3 (`CLAUDE.md` "iOS hosts AUv3 only"). Nesting AUv3 inside AUv3 is sandbox-fragile. |

Underneath all five shapes is the same `OTTOPlugin` JUCE static-library "shared code" target (created by `juce_add_plugin(OTTOPlugin)` at `juce_add_plugin` line 2196 of `external/JUCE/extras/Build/CMake/JUCEUtils.cmake` → `add_library(${target} STATIC)`). The shared-code target compiles `PluginProcessor.cpp` and dependencies, and the format-specific wrappers (`_Standalone`, `_VST3`, …) link against it plus their respective format-API glue.

**The wall the plan hit:** the shared-code target is buildable in OTTO's own CMake context but isn't reachable from IDA. IDA's `add_subdirectory` would have to wrap `juce_add_plugin(OTTOPlugin)` in OTTO's full top-level config (`OTTOConfig.cmake`, `Platforms.cmake`, `Dependencies.cmake`, the OTTO_* variable surface), all of which are bound up with the plugin-bundle generation IDA does not want and the iOS branches IDA can't run in its own context.

---

## 3. The sixth shape: `OTTOEngine`

A new OTTO build shape — purpose-built for embedding inside another C++/JUCE application that ships as part of the same product family. Specifically:

| Property | Value | Rationale |
|---|---|---|
| CMake target name | `OTTOEngine` (alias `otto::engine`) | Distinguishes from `OTTOPlugin` (which still exists for the five other shapes). |
| CMake library type | `STATIC` | IDA links it as part of `IdaOttoBridge`. |
| Source set | Identical to `OTTOPlugin` — reuses `OTTO_SHARED_PLUGIN_SOURCES` + `OTTO_SHARED_CORE_SOURCES` from `cmake/SharedPluginSources.cmake` | Single source of truth preserved — when OTTO adds a plugin .cpp file, OTTOEngine picks it up automatically. |
| JUCE module deps | Identical to `OTTOPlugin` — every `juce::juce_*` link in `otto-plugin/CMakeLists.txt:190-202` | The shared code expects all of them. Reducing the set risks silent ODR / symbol-missing failures at link time. |
| Plugin-format SDKs | NONE — no VST3, no AU, no CLAP, no AAX, no Standalone wrapper, no `juce::juce_audio_plugin_client` | Not a plugin. Doesn't need format SDKs. |
| Plugin-config compile defs | Minimal subset — just `JucePlugin_Name="OTTO"` (and whatever OTHER `JucePlugin_*` macros the shared code references) | The shared code references `JucePlugin_Name` in `PluginProcessor.h:70` (`getName()` override). All other `JucePlugin_*` macros set by `juce_add_plugin` are format-metadata for bundle generation — irrelevant when there's no bundle. |
| Transport authority | OTTO's internal `TransportTracker` — same as Standalone | OttoEngine ships with OTTO's self-driven transport semantics. IDA never feeds an `AudioPlayHead` to this instance. |
| iOS support | YES | OTTOEngine's source set is identical to OTTOPlugin's; iOS-specific branches (e.g. `OTTO_SHARED_IOS_SOURCES`) are gated by `OTTO_PLATFORM_IOS` and OTTOEngine respects the same gate. |
| Asset bundling | NONE — OTTOEngine emits no `OTTOPluginAssets` binary-data lib | IDA handles asset bundling separately per `CLAUDE.md` ("IDA's build references the operator's OTTO checkout at `/Users/larryseyer/AudioDevelopment/OTTO/assets/`"). |

OTTOEngine is the simplest possible "library form" of OTTO's audio engine. It is the OTTO that PART-of-IDA framing implies: a static library, embedded, no SDK ceremony.

---

## 4. Decision log — Options A through E

Five distinct paths were considered. Each is recorded here with explicit rejection reasoning so a future session does not re-litigate the same fork.

### Option A — Link `OTTOPlugin` via `add_subdirectory(otto-plugin)` from IDA — REJECTED

**Approach:** IDA's `cmake/Dependencies.cmake` calls `add_subdirectory("${OTTO_PATH}/src/otto-plugin" ...)` after pre-setting the ~15 OTTO_* CMake variables the existing CMakeLists references.

**Rejection reasons:**
1. The CMakeLists at `external/OTTO/src/otto-plugin/CMakeLists.txt` uses `${CMAKE_SOURCE_DIR}` in 6+ places (line 96 `include(${CMAKE_SOURCE_DIR}/cmake/SharedPluginSources.cmake)`, asset paths, etc.). In IDA's context `CMAKE_SOURCE_DIR` is IDA's root, so the includes resolve to nonexistent IDA paths.
2. `juce_add_plugin(OTTOPlugin ...)` triggers plugin-format target generation (`OTTOPlugin_Standalone`, `OTTOPlugin_VST3`, `OTTOPlugin_AU`, `OTTOPlugin_CLAP`) plus their format-SDK link chains. IDA does not want these built.
3. Conditional iOS / AAX branches assume OTTO's own platform context (`OTTO_PLATFORM_IOS`, `OTTO_AAX_AVAILABLE`). Setting them correctly from IDA requires duplicating OTTO's `Platforms.cmake` logic in IDA's build, which is exactly the cross-project drift this whole arrangement avoids.

### Option B — Compile OTTO's shared plugin sources directly into `IdaOttoBridge` — REJECTED

**Approach:** Add `${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-plugin/PluginProcessor.cpp` plus the 25+ other shared .cpp files listed in `SharedPluginSources.cmake` to IdaOttoBridge's source list, define the necessary `JucePlugin_*` macros, link the JUCE modules manually.

**Rejection reasons:**
1. Breaks OTTO's "single source of truth" promise — IDA-side `IdaOttoBridge` CMakeLists would need to be kept in lock-step with OTTO's `SharedPluginSources.cmake` whenever OTTO adds or removes a plugin .cpp file. This is exactly the drift the submodule + add_subdirectory pattern was built to prevent.
2. Has the same `${CMAKE_SOURCE_DIR}` resolution problem as Option A.
3. Diagnostic surface is awful — when a file fails to compile, the operator has to mentally translate "this is OTTO's plugin code building inside IDA's bridge" and inspect both repos.

### Option C — Revisit the brainstorm; rethink S1's design — REJECTED

**Approach:** Drop "embed OTTOProcessor" and go back to BS-3. Maybe stay with `PlayerManager`+`TransportTracker` and find a different way to bring in Conductor + Pattern + MIDI dispatch.

**Rejection reasons:**
1. The locked decisions remain valid. The brainstorm's architectural conclusion (embed the full processor) is the right one — it's only the CMake plumbing that the original plan missed.
2. Alternative approaches (porting OTTO's per-block driving logic into the bridge) immediately violate "single source of truth."
3. Staying with `PlayerManager`-only postpones the audibility unblock that S2 depends on. The CMake fix exists (Option D); rebooting the brainstorm spends design budget on a problem we already solved.

### Option D — Add a new `OTTOEngine` static-library target to OTTO — CHOSEN

**Approach:** OTTO grows a sixth build shape (this document). New `src/otto-engine/CMakeLists.txt` defines a STATIC library that compiles the same shared sources OTTOPlugin uses, links the same JUCE module set, declares no plugin-format dependencies, and exposes `OTTOProcessor` as a regular C++ symbol. IDA `add_subdirectory`s only the new `otto-engine` directory (NOT OTTO's top-level CMakeLists), so OTTO's plugin-config bookkeeping is not pulled in.

**Why this wins:**
1. **Source-of-truth preserved.** `SharedPluginSources.cmake` is parameterized (one tiny edit) so both OTTOPlugin and OTTOEngine consume the same source list. OTTO contributors add plugin .cpps in one place; both targets pick them up.
2. **No format-SDK overhead.** OTTOEngine builds zero plugin-format wrappers. The compilation graph is just shared sources + JUCE modules + otto-core.
3. **No top-level OTTO config dependency.** `add_subdirectory(otto-engine)` from IDA does NOT require `OTTOConfig.cmake`, `Platforms.cmake`, `OTTO_PRODUCT_NAME`, or any OTTO_* variable. OTTOEngine's CMakeLists is self-contained and computes its own paths via `CMAKE_CURRENT_SOURCE_DIR`.
4. **iOS-safe.** OTTOEngine's source set respects the same `OTTO_PLATFORM_IOS` gate the shared sources already use. No AUv3 nesting since OTTOEngine is a library, not a plugin extension.
5. **Honest framing.** OTTOEngine matches the `project_otto_is_part_of_ida_not_a_plugin` locked decision. A library called "OTTOEngine" is not a plugin; nothing about the embed asks the user or any tool to think of it as one.
6. **Cross-project change cost is bounded.** OTTO-side: one new file (~30 lines of CMake) plus one paramaterization edit in `SharedPluginSources.cmake`. No source code changes. No behavioral changes to OTTO's existing five shapes.

### Option E — Copy OTTO's transport into IDA, host OTTO as a VST3 — REJECTED

**Approach:** Port OTTO's `TransportTracker` and supporting code into IDA so IDA has its own self-driven transport. Then host OTTO as a regular VST3 plugin via IDA's existing plugin-host infrastructure, feeding the host transport down to OTTO via `juce::AudioPlayHead`.

**Rejection reasons:**
1. **Contradicts three locked decisions.** `project_otto_is_part_of_ida_not_a_plugin` says don't frame OTTO as a plugin. `project_otto_does_not_host_plugins` (the spirit, anyway — IDA hosts plugins on output-mixer strips, not as the embedded-OTTO surface). `project_otto_is_the_transport_source` says OTTO supplies transport to IDA, not the other way around. All three would have to be explicitly revisited.
2. **Whitepaper V10 §5.7 would need rewording.** The §5.7 commitments — "OTTO not a hosted plugin," "OTTO supplies tempo-map data," etc. — would have to be rewritten to admit the plugin framing.
3. **Transport-code duplication = forever sync work.** Every OTTO transport fix (BPM quantization, time-signature handling, energy-level transitions) must be manually reapplied IDA-side. Defeats the submodule pattern's whole point.
4. **Plugin-format wrappers strip features.** OTTO's plugin builds defer file dialogs to the host, lose Standalone-only surfaces, and have flat parameter automation vs. OTTO's rich state model. Loading an OTTO Song bundle via hosted-VST3-OTTO is fiddlier than via embedded-OTTOEngine.
5. **iOS AUv3 nesting.** IDA ships AUv3 on iOS. Hosting an AUv3 OTTO inside an AUv3 IDA is sandbox-fragile (each AUv3 has its own container; asset-path resolution breaks).
6. **Plugin scanner is broken right now** (`project_plugin_scanner_broken`). Programmatic loading would bypass the scanner UI but would still hit underlying scanner bugs.
7. **Out-of-process hosting cost.** IDA's plugin hosting is out-of-process (`OutOfProcessEffectChainHostSupervisor`). Running OTTO out-of-process would add IPC latency to the audio loop for every MIDI dispatch + every sample buffer hand-off. Heavy vs. an in-process linked engine.

Option E would be the right call if OTTO needed sandbox isolation, or if users needed to mix-and-match OTTO versions independent of IDA versions. Neither applies — same operator, same ship cadence, same submodule pin.

---

## 5. OTTO-side implementation

Two file changes in OTTO. Cross-project commit per `external/OTTO/CLAUDE.md` Cross-Project Inbox Protocol; the IDA-Origin trailer carries the IDA SHA that bumps the submodule.

### 5.1 `cmake/SharedPluginSources.cmake` — parameterize the root

Today the file hardcodes `${CMAKE_SOURCE_DIR}/src/otto-plugin` and `${CMAKE_SOURCE_DIR}/src/otto-core/...`. When OTTO's own top-level builds it, `CMAKE_SOURCE_DIR` is OTTO's root. When IDA `add_subdirectory`s `otto-engine` directly, `CMAKE_SOURCE_DIR` is IDA's root and the paths break.

The fix is one-line additive:

```cmake
# external/OTTO/cmake/SharedPluginSources.cmake — top of file
if(NOT DEFINED OTTO_SHARED_SOURCES_ROOT)
    set(OTTO_SHARED_SOURCES_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(OTTO_PLUGIN_DIR "${OTTO_SHARED_SOURCES_ROOT}/src/otto-plugin")
# ... all subsequent ${CMAKE_SOURCE_DIR} references are textually replaced
# with ${OTTO_SHARED_SOURCES_ROOT} ...
```

OTTO's own plugin build is unaffected because `OTTO_SHARED_SOURCES_ROOT` defaults to `${CMAKE_SOURCE_DIR}` when not set (which is what OTTO's top-level cmake configure leaves it as). OTTOEngine's CMakeLists sets `OTTO_SHARED_SOURCES_ROOT` to OTTO's source root (computed via `CMAKE_CURRENT_SOURCE_DIR`) before including the file.

### 5.2 New: `src/otto-engine/CMakeLists.txt`

Self-contained CMakeLists for the new target. ~30 lines. Approximately:

```cmake
# =============================================================================
# OTTOEngine — embedded static library for in-process consumers
# =============================================================================
# OTTO's audio engine packaged as a STATIC C++ library. Use when another
# C++/JUCE application wants to embed OTTO in-process (as opposed to hosting
# it as a VST3/AU/CLAP/AUv3 plugin or running it as a Standalone app).
#
# Distinguishing facts:
#  - No plugin-format SDK dependencies (no VST3 SDK, no AU API, no CLAP, no
#    AAX, no Standalone wrapper).
#  - No plugin-bundle generation. Just a .a/.lib that consumers link.
#  - Transport authority stays with OTTO's internal TransportTracker — the
#    embedding app does NOT feed a juce::AudioPlayHead.
#  - Source set is identical to OTTOPlugin (consumes the same
#    SharedPluginSources.cmake list) — single source of truth.
# =============================================================================

# Compute OTTO's source root relative to this CMakeLists so add_subdirectory
# from another project (with a different CMAKE_SOURCE_DIR) still resolves
# the shared-sources paths correctly.
get_filename_component(OTTO_ENGINE_OTTO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Parameterize SharedPluginSources.cmake to consume from OTTO's tree, not
# the calling project's tree.
set(OTTO_SHARED_SOURCES_ROOT "${OTTO_ENGINE_OTTO_ROOT}")
include("${OTTO_ENGINE_OTTO_ROOT}/cmake/SharedPluginSources.cmake")

add_library(OTTOEngine STATIC
    ${OTTO_SHARED_PLUGIN_SOURCES}
    ${OTTO_SHARED_CORE_SOURCES})

add_library(otto::engine ALIAS OTTOEngine)

target_include_directories(OTTOEngine
    PUBLIC
        # OTTOProcessor + PluginEditor headers — consumers include
        # <PluginProcessor.h> directly.
        "${OTTO_ENGINE_OTTO_ROOT}/src/otto-plugin")

# JUCE modules: identical link set to OTTOPlugin (see otto-plugin/CMakeLists.txt:190+).
# Reducing this list risks silent ODR / symbol-missing failures.
target_link_libraries(OTTOEngine
    PUBLIC
        otto-core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
        juce::juce_cryptography
        lsfx::lsfx_tapecolor
        Ableton::Link)

# Plugin-config compile defs that the SHARED CODE references. JucePlugin_Name
# is read by PluginProcessor.h::getName() (line 70). Other JucePlugin_* macros
# set by juce_add_plugin are bundle metadata — not needed by the library form.
# JUCE_STANDALONE_APPLICATION=0 disables Standalone-only code paths.
target_compile_definitions(OTTOEngine
    PUBLIC
        JucePlugin_Name="OTTO"
        JUCE_STANDALONE_APPLICATION=0
        # Marker for any future shared-code branch that needs to know it's
        # running inside the embedded library form (vs. a plugin or
        # standalone). Currently unused; reserved.
        OTTO_EMBEDDED_BUILD=1)
```

The macro budget at the bottom is the smallest set that compiles cleanly today. If OTTO's shared code adds a new `JucePlugin_*` macro reference in the future, OTTOEngine's compile defs grow by exactly one line. This is the only ongoing maintenance overhead — and it's caught at the first failing build, not silently.

### 5.3 Optional: `add_subdirectory(src/otto-engine)` in OTTO's top-level

Optional because IDA `add_subdirectory`s OTTOEngine directly, bypassing OTTO's top-level. But adding the line to OTTO's top-level CMakeLists makes OTTOEngine buildable from OTTO's own build directory (useful for OTTO-side CI and for any future OTTO-internal consumer). Gated behind an option for safety:

```cmake
# external/OTTO/CMakeLists.txt — add after line 99 (add_subdirectory(src/otto-plugin))
option(OTTO_BUILD_EMBEDDED_ENGINE
    "Build OTTOEngine static library (for embedding inside another C++/JUCE app)" OFF)

if(OTTO_BUILD_EMBEDDED_ENGINE)
    add_subdirectory(src/otto-engine)
endif()
```

Default OFF so OTTO's existing build behavior is byte-identical. IDA enables it implicitly by doing its own `add_subdirectory`.

### 5.4 What does NOT change

- Existing OTTOPlugin target — same source list, same compile defs, same plugin-format wrappers, same behavior. OTTOEngine is purely additive.
- Existing CROSS_PROJECT_INBOX entries.
- OTTO's `processBlock`, `TransportTracker`, `PlayerManager::getGlobalMixer`, or any audio-thread code.
- OTTO's `Ida-Origin` trailer convention.
- OTTO's tests.

---

## 6. IDA-side integration

Two file changes in IDA, one submodule bump. All staged into one atomic commit per `CLAUDE.md` commit discipline.

### 6.1 `cmake/Dependencies.cmake` — pull in OTTOEngine

After the existing `add_subdirectory("${OTTO_CORE_PATH}" "${CMAKE_BINARY_DIR}/otto-core")` at line 233, add:

```cmake
# OTTOEngine — OTTO's audio engine as a STATIC library. Provides
# OTTOProcessor (the juce::AudioProcessor subclass) and its full transitive
# dependency chain (Conductor, Pattern engine, MIDI dispatch, sampler voices,
# internal FX, GlobalMixer). IDA's OttoHost embeds one instance and drives
# its processBlock per audio buffer. See
# docs/superpowers/specs/2026-05-27-otto-engine-static-target.md for why
# this exists as a separate shape from OTTOPlugin.
set(OTTO_ENGINE_PATH "${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-engine")
add_subdirectory("${OTTO_ENGINE_PATH}" "${CMAKE_BINARY_DIR}/otto-engine")
message(STATUS "otto-engine configured from: ${OTTO_ENGINE_PATH}")
```

### 6.2 `otto-bridge/CMakeLists.txt` — link `otto::engine` + add include dir

The plan's draft already covered this; the only correction is the target name. `OTTOPlugin` → `otto::engine`:

```cmake
target_link_libraries(IdaOttoBridge
    PUBLIC  Ida::Core
    PRIVATE otto::core
            otto::engine            # ← S1: provides OTTOProcessor
            juce::juce_audio_basics
            juce::juce_audio_processors  # ← S1: juce::AudioProcessor base
            juce::juce_events
            lsfx::lsfx_tapecolor
            Ida::Engine)
```

`otto::engine` exports `src/otto-plugin/` as a PUBLIC include directory (set in 5.2 above), so no extra IDA-side include-path plumbing is needed — `#include <PluginProcessor.h>` from `otto-bridge/src/OttoHost.cpp` resolves through the linked target.

`JucePlugin_Name` propagates via `otto::engine`'s PUBLIC compile defs, so IDA does NOT need to define it locally (unlike the failed first attempt which defined it inside `IdaOttoBridge` PRIVATE compile defs as a workaround).

### 6.3 `otto-bridge/src/OttoHost.cpp` — embed `OTTOProcessor`

The refactor described in the original S1 plan (`plans/2026-05-27-otto-host-embeds-ottoprocessor.md` Task 3) is still correct in shape — the only change is the include path now resolves through `otto::engine` and the link target name is `otto::engine` not `OTTOPlugin`. The five edits (includes, Impl struct + scratch buffer, prepare, renderBlock, accessors) translate verbatim.

One small correction the failed attempt surfaced: the member-declaration order in `Impl` must match the initializer-list order to avoid `-Wreorder` under `-Werror`. The corrected order is `processor` first (declaration), `transportRing` second; `subscription` LAST so it destroys FIRST. The original plan reversed declaration vs. init-list ordering inadvertently.

### 6.4 `external/OTTO` submodule pointer

Bumped to the OTTO commit that introduces `otto-engine/CMakeLists.txt` + the `SharedPluginSources.cmake` paramaterization edit. Staged with the IDA-side files for a single atomic commit per `CLAUDE.md` commit discipline.

### 6.5 `tests/OttoHostRenderTests.cpp` — append the embed-pin regression test

The new `[otto-host-render][processor-embed]` case from the original plan is unchanged. The existing 6 cases / 157 assertions remain the regression baseline.

### 6.6 What does NOT change

- `otto-bridge/include/ida/OttoHost.h` — public header byte-identical. Slice 4b's external API shape is preserved.
- `app/MainComponent.cpp:4274` (`ottoHost_ = std::make_unique<ida::OttoHost>();`) and `:4281` (`audioCallback_->setOttoRenderSource(ottoHost_.get());`) — untouched.
- Existing 12 OTTO-host tests (6 render + 6 transport) — pass as regression baseline.
- ctest baseline of 790/791.

---

## 7. Cross-project coordination

### 7.1 Sequencing

1. OTTO-side commit lands first (creates `otto-engine/`, parameterizes `SharedPluginSources.cmake`, appends inbox entry under `[FROM IDA → OTTO]`).
2. OTTO is pushed to `origin/main`.
3. IDA bumps the submodule SHA + lands the IDA-side bridge + test changes in one atomic commit + pushes.

This order matters: between steps 1 and 3, IDA's tree still pins the pre-OTTOEngine OTTO SHA, so the build still works (just without OTTOEngine available). No window of brokenness.

### 7.2 Collision with the concurrent TAPECOLOR OTTO session

A parallel Claude session is working on OTTO scoped to TAPECOLOR (operator-confirmed 2026-05-27). TAPECOLOR's edit surface is `external/lsfx_tapecolor/` and OTTO's TAPECOLOR adapter — NOT `src/otto-plugin/CMakeLists.txt`, `cmake/SharedPluginSources.cmake`, or the new `src/otto-engine/` directory. File-level collision is unlikely.

The real coordination risk is concurrent pushes to OTTO's `origin/main` causing a non-fast-forward. Mitigations:

- Stage exact paths on the OTTO commit (`git add cmake/SharedPluginSources.cmake src/otto-engine/CMakeLists.txt CROSS_PROJECT_INBOX.md`). Never `git add -A` on OTTO. This protects against accidentally sweeping in the TAPECOLOR session's WIP.
- Before pushing OTTO, `git pull --rebase origin main`. If TAPECOLOR landed first, rebase will apply our two files cleanly on top (zero overlap in touched paths).
- If rebase conflicts on `CROSS_PROJECT_INBOX.md` (TAPECOLOR also appended an entry), resolve by stacking both entries — never delete the other session's entry.

### 7.3 Inbox entry template (for the OTTO commit)

```
### 2026-05-27 — IMPLEMENTATION: OTTOEngine static-library target for IDA embedding (Option D)

Direction: IDA → OTTO
IDA commit: (filled at IDA-side atomic commit time)
OTTO commit: <this commit>
Files touched:
- cmake/SharedPluginSources.cmake (paramaterize root path via OTTO_SHARED_SOURCES_ROOT; backward-compatible default keeps OTTO's own build unchanged)
- src/otto-engine/CMakeLists.txt (new — STATIC library wrapping the shared
  plugin sources without plugin-format SDK dependencies; ~70 lines)
- CMakeLists.txt (optional — add gated add_subdirectory(src/otto-engine) under OTTO_BUILD_EMBEDDED_ENGINE option, default OFF)

Why: IDA's S1 slice needs OTTOProcessor's full processBlock pipeline embedded in-process. None of OTTO's five existing shapes fit (Standalone is an executable; the four plugin shapes defer transport to the host). OTTOEngine is the sixth shape — a STATIC library version of the shared code with self-driven transport. See docs/superpowers/specs/2026-05-27-otto-engine-static-target.md in IDA for the full design + decision log.

For OTTO's Claude: do NOT revert. OTTOPlugin and its five format targets are untouched — OTTOEngine is purely additive. SharedPluginSources.cmake's parameterization is backward-compatible (defaults preserve OTTO's existing builds byte-identically). When refactoring shared plugin sources in the future, both OTTOPlugin and OTTOEngine will pick up the changes automatically; no second-target maintenance burden.

Status: needs-ack
Resolution: (filled when OTTO's next session reviews)
```

---

## 8. Build, test, and verification contract

### 8.1 OTTO-side acceptance (before pushing OTTO)

- `git -C external/OTTO diff --stat` shows exactly two modified files + one new file (the OTTO-engine CMakeLists, optionally plus the top-level `add_subdirectory` line under the option).
- OTTO's own existing build (if invokable) continues to succeed with OTTOEngine not built (`OTTO_BUILD_EMBEDDED_ENGINE` is OFF by default). Skip this if OTTOConfig.cmake is unavailable locally — see §11 footnote.
- The inbox entry under `[FROM IDA → OTTO]` is appended and committed atomically with the code changes.

### 8.2 IDA-side acceptance (before pushing IDA)

- `cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release` succeeds with no new warnings beyond pre-existing baseline.
- `cmake --build build --target IdaOttoBridge` succeeds. `build/otto-bridge/libIdaOttoBridge.a` exists.
- `cmake --build build --target IdaTests` succeeds. The link line shows `otto-engine/libOTTOEngine.a` (or platform equivalent) in the link list — NOT a bare `-lOTTOPlugin` or `-lOTTOEngine`.
- `cd build && ctest --output-on-failure -R "OttoHost"` passes all 12 existing OTTO-host cases (6 render + 6 transport) + the 1 new `[otto-host-render][processor-embed]` case = 13 cases total.
- `ctest --test-dir build` reports **790 passed, 1 not-run** — the pre-S1 baseline preserved exactly.
- `cmake --build build --target IDA` succeeds. `build/app/IDA_artefacts/Release/IDA.app` builds.
- `git diff` against pre-S1 baseline on `otto-bridge/include/ida/OttoHost.h` is EMPTY (public header byte-identical).

### 8.3 Operator-eyes verification (optional, deferred to S2)

S1 produces no operator-visible change — the engine swap is invisible at the UI layer. Audible verification of the embedded OTTO is S2's responsibility (`OttoPane` tab via `OTTOProcessor::createEditor()`).

---

## 9. Transport authority (restated)

OTTO retains transport authority in the embed. Concretely:

- OTTOEngine's `OTTOProcessor` instance owns a `TransportTracker` (same as Standalone OTTO).
- `processBlock` advances the transport. IDA does not feed a `juce::AudioPlayHead` to the embedded processor.
- OTTO's `TransportTracker` publishes `TransportEvent` to the singleton `otto::EventBus`.
- IDA's `OttoHost::Impl` subscribes (existing subscription, unchanged by S1) and marshals events through the SPSC ring to the message-thread `IOttoTransportListener` fan-out.
- IDA's transport bar (S3 work) READS this state to render Play / Stop / BPM / position. The transport bar's Play / Stop / BPM / time-sig set actions WRITE via a thin wrapper that calls `OTTOProcessor::getTransportState()` setters (`setPlaying`, `setBpm`, etc.).

This preserves `project_otto_is_the_transport_source` exactly. The persistent transport bar described in the architecture spec is a VIEW + COMMAND surface over OTTO's transport — not a separate transport-of-its-own that OTTO slaves to.

---

## 10. Conformance to locked decisions

| Locked decision | How OTTOEngine conforms |
|---|---|
| `project_otto_integration_locked_decisions` — OttoHost embeds OTTOProcessor | YES — OttoHost owns a `unique_ptr<OTTOProcessor>` via `otto::engine` link. |
| `project_otto_is_part_of_ida_not_a_plugin` | YES — OTTOEngine is a static library, not a plugin. No VST3 / AU / CLAP / AUv3 wrapping. |
| `project_otto_does_not_host_plugins` | UNCHANGED — IDA still hosts 3rd-party plugins on output-mixer strips; OTTO's internal FX still run inside OTTO. OTTOEngine doesn't host plugins either. |
| `project_otto_is_the_transport_source` | YES — see §9. OTTOEngine retains OTTO's self-driven TransportTracker authority. |
| `project_otto_is_a_submodule_now` | YES — OTTOEngine lives under the OTTO submodule. Single source of truth preserved. |
| `project_otto_as_output_mixer_source` | YES — the 32 stereo per-output accessors still read through `processor->getPlayerManager().getGlobalMixer()`. |
| `project_otto_assets_out_of_git` | UNCHANGED — OTTOEngine emits no asset bundle. Asset path resolution is IDA's responsibility (same as today). |
| Whitepaper V10 §5.7 "OTTO as bundled rhythm engine and tempo-map source" | YES — bundled engine, not a hosted plugin. OTTOEngine is the engineering realization. |
| Whitepaper V10 §4.2 "OTTO not a discipline source for LMC" | UNCHANGED — OTTO supplies tempo-map data via the boundary-conversion rule; LMC discipline tier list untouched. |
| `CLAUDE.md` "Simplicity first" | YES — smallest cross-project surgery that achieves the locked goals. ~30 lines of new OTTO CMake + 1 paramaterization line. |
| `CLAUDE.md` "Surgical changes" | YES — additive only on the OTTO side; no behavior changes to existing five shapes. |
| `CLAUDE.md` "No silent failures" | YES — every step has a verification check in §8. |

---

## 11. Notes for future Claude sessions

### What you should remember

- **OTTO has six build shapes now (after this lands): Standalone, VST3, AU, CLAP, AUv3, OTTOEngine.** The first five are plugin-format outputs (Standalone is technically an .app wrapping the plugin code, but produces a runnable executable). OTTOEngine is a STATIC library for embedding inside another C++/JUCE application.
- **`OTTOEngine` is consumed only by IDA today** — but it's a reusable shape. If a future OTTO-derived app wants to embed OTTO in-process, OTTOEngine is the answer.
- **Transport authority is in OTTO** (per `OTTOProcessor`'s internal `TransportTracker`), regardless of which shape is running. The plugin shapes' deference to `juce::AudioPlayHead` is JUCE convention, not an OTTO design choice — OTTOEngine bypasses it because IDA does not provide a PlayHead.
- **Source-of-truth lives in `external/OTTO/cmake/SharedPluginSources.cmake`** — both OTTOPlugin and OTTOEngine consume the same source list. Adding a new plugin .cpp file in OTTO automatically rebuilds both targets.

### What you should NOT do

- **Do NOT add `add_subdirectory(otto-plugin)` to IDA's CMake.** That path was explored as Option A and rejected. The OTTOEngine target exists specifically so this is not necessary.
- **Do NOT copy OTTO source files into IDA's tree.** That path was Option B, rejected.
- **Do NOT define `JucePlugin_Name` (or other `JucePlugin_*` macros) inside IDA's `IdaOttoBridge` CMakeLists.** Those macros are owned by OTTOEngine's PUBLIC compile defs and propagate to consumers automatically. Defining them in IDA risks ODR conflict with the shared sources' expectations.
- **Do NOT feed a `juce::AudioPlayHead` to the embedded OTTOProcessor.** Transport authority stays with OTTO's internal TransportTracker. Feeding a PlayHead would change OTTO's transport semantics from self-driven to host-driven and break the §9 contract.
- **Do NOT bypass the cross-project inbox protocol** when editing OTTO source. Even single-character edits go through the inbox with the `Ida-Origin:` trailer per `external/OTTO/CLAUDE.md`.

### Footnote: OTTOConfig.cmake

OTTO's top-level CMakeLists includes `cmake/OTTOConfig.cmake` (line 84) which doesn't exist in the OTTO repository. OTTO's own standalone `cmake configure` would fail at this include unless the operator has a local OTTOConfig.cmake that's not under version control. This is a pre-existing OTTO bug unrelated to S1 and OTTOEngine. **OTTOEngine is unaffected** because IDA `add_subdirectory`s `src/otto-engine` directly (skipping OTTO's top-level), and OTTOEngine's CMakeLists is self-contained (no OTTO_* variable dependency). If you encounter the missing OTTOConfig.cmake while doing OTTO-side work, surface it as a separate issue — do NOT add a stub OTTOConfig.cmake as a side-effect of this work.

---

## 12. Self-review checklist

- [x] **Captures the problem.** §0 explains why the original plan failed. §1 lists the inflexible architectural constraints.
- [x] **Justifies the decision.** §4 walks through all five options with explicit rejection reasoning. Future sessions won't relitigate.
- [x] **Specifies the OTTO-side surgery exactly.** §5 names files, lines, and approximate code shapes.
- [x] **Specifies the IDA-side surgery exactly.** §6 names files, lines, and code shapes.
- [x] **Handles the concurrent-OTTO-session risk.** §7.2 details the protocol.
- [x] **Defines acceptance.** §8 enumerates exact CMake and ctest commands + expected outputs.
- [x] **Restates transport authority.** §9 makes the OTTO-owns-transport contract explicit.
- [x] **Tables conformance to locked decisions.** §10 walks through every relevant memory entry.
- [x] **Anticipates future-Claude pitfalls.** §11 enumerates the most likely "what should I NOT do" mistakes.
- [x] **Documents the OTTOConfig.cmake oddity** so future sessions don't waste time chasing it.
- [x] **No placeholders.** No "TBD" / "TODO" / "implement later." Every section is concrete.

---

## 13. Next steps

1. Operator reviews this spec (BS-6).
2. If approved: a new implementation plan supersedes `plans/2026-05-27-otto-host-embeds-ottoprocessor.md` via `superpowers:writing-plans`. The new plan's task structure differs from the old one in T1 only — T1 becomes "create OTTOEngine target + paramaterize SharedPluginSources.cmake + push OTTO" instead of "add getPlayerManager() accessor."
3. The new plan is executed.

The IDA-side T3 / T4 / T5 changes from the old plan remain accurate at the source-code level — they just need the link target name corrected (`OTTOPlugin` → `otto::engine`) and the include-path workaround dropped (OTTOEngine exports the include dir publicly).
