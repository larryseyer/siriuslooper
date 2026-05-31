# S1 (re-planned) — OttoHost embeds OTTOProcessor via OTTOEngine static lib

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Plan file (canonical location):** copy this plan to `docs/superpowers/plans/2026-05-27-otto-engine-embed.md` as Task 0 below (in-tree, atomic with the S1 commit). The system-required write location for plan mode is `~/.claude/plans/read-continue-md-read-shiny-elephant.md` — both files are identical; the in-tree copy is the durable artifact.

---

## Context

**Why this exists:** Slice S1 of the broader OTTO integration architecture (docs/superpowers/specs/2026-05-27-otto-integration-architecture.md §1.2 commitment 2) embeds OTTO's full `OTTOProcessor` inside IDA's `OttoHost`, bringing OTTO's Conductor + Pattern engine + MIDI dispatch + internal FX + GlobalMixer + self-driven `TransportTracker` into IDA in-process. This unblocks S2 (OttoPane tab via `OTTOProcessor::createEditor()`), S3 (transport bar), S4 (preset state).

**Why the previous plan failed:** `plans/2026-05-27-otto-host-embeds-ottoprocessor.md` (HEAD `d5d4fae`, now invalidated) assumed IDA could link OTTO's `OTTOPlugin` shared-code static lib directly, but IDA's `cmake/Dependencies.cmake:232` only `add_subdirectory`s `otto-core` — OTTOPlugin is unreachable from IDA's build. The original plan also assumed it needed to add a `getPlayerManager()` accessor to OTTOProcessor; that accessor already exists at `external/OTTO/src/otto-plugin/PluginProcessor.h:361-362`.

**What's locked (Option D, 2026-05-27, full design in `docs/superpowers/specs/2026-05-27-otto-engine-static-target.md`):**
- OTTO grows a sixth build shape — `OTTOEngine` — a STATIC library (alias `otto::engine`) that wraps the same shared plugin sources `OTTOPlugin` uses, without any plugin-format SDK, app wrapper, or asset bundle.
- `cmake/SharedPluginSources.cmake` is parameterized so both `OTTOPlugin` and `OTTOEngine` can include it (single source of truth preserved).
- IDA `add_subdirectory`s `external/OTTO/src/otto-engine` directly (skipping OTTO's top-level CMakeLists and its OTTO_* variable surface and its pre-existing missing `OTTOConfig.cmake` reference).
- OTTO retains transport authority via `OTTOProcessor`'s internal `TransportTracker`; IDA never feeds a `juce::AudioPlayHead`.
- Public `OttoHost.h` surface is preserved byte-identical so `app/MainComponent.cpp:4274` + `:4281` need zero edits.
- ctest baseline 790/791 must be preserved; one new `[otto-host-render][processor-embed]` regression case appended.

**Scope check:** S1 only. S2–S7 each get their own plan when reached (per the architecture spec §7).

**Cross-project coordination:** A concurrent OTTO Claude session is scoped to TAPECOLOR (touches `external/lsfx_tapecolor/` + OTTO's TAPECOLOR adapter — NOT `cmake/SharedPluginSources.cmake` or `src/otto-engine/`). File-level collision is unlikely. The collision protocol is: stage exact paths on the OTTO commit, `git pull --rebase origin main` before push, stack inbox entries on conflict (never delete the other session's entry).

---

## File Structure

### Cross-project (OTTO submodule at `external/OTTO/`)

**Modify:**
- `external/OTTO/cmake/SharedPluginSources.cmake` — top of file: introduce `OTTO_SHARED_SOURCES_ROOT` defaulting to `${CMAKE_SOURCE_DIR}`; replace every `${CMAKE_SOURCE_DIR}/src/...` reference with `${OTTO_SHARED_SOURCES_ROOT}/src/...`. Backward-compatible: OTTO's existing builds resolve identically.
- `external/OTTO/CMakeLists.txt` — append `option(OTTO_BUILD_EMBEDDED_ENGINE …)` + gated `add_subdirectory(src/otto-engine)` after the existing `add_subdirectory(src/otto-plugin)` at line 94. Default OFF — OTTO's own builds unchanged.
- `external/OTTO/CROSS_PROJECT_INBOX.md` — append `[FROM IDA → OTTO]` entry.

**Create:**
- `external/OTTO/src/otto-engine/CMakeLists.txt` — self-contained STATIC library target `OTTOEngine` (alias `otto::engine`) consuming `OTTO_SHARED_PLUGIN_SOURCES` + `OTTO_SHARED_CORE_SOURCES` from the parameterized SharedPluginSources, linking the same JUCE module set OTTOPlugin uses (`otto-plugin/CMakeLists.txt:190-213`), with PUBLIC include dir `src/otto-plugin/` and PUBLIC compile defs `JucePlugin_Name="OTTO"` + `JUCE_STANDALONE_APPLICATION=0` + `OTTO_EMBEDDED_BUILD=1`.

### IDA-side

**Modify:**
- `cmake/Dependencies.cmake:233` — append `set(OTTO_ENGINE_PATH …)` + `add_subdirectory(otto-engine)` immediately after the existing `otto-core` block.
- `otto-bridge/CMakeLists.txt:31-42` — add `otto::engine` + `juce::juce_audio_processors` to the PRIVATE link list.
- `otto-bridge/src/OttoHost.cpp` — refactor `Impl` from bare `PlayerManager`+`TransportTracker` to `std::unique_ptr<OTTOProcessor>` + scratch `juce::AudioBuffer<float>` + `juce::MidiBuffer` + `cachedMaxBlock` + `prepared` flag; rewrite `prepare`, `isPrepared`, `renderBlock`, and the two `getOttoOutput{Left,Right}` accessors to drive via `processor->processBlock` and read via `processor->getPlayerManager().getGlobalMixer()`. Public header `otto-bridge/include/ida/OttoHost.h` is unchanged.
- `tests/OttoHostRenderTests.cpp` — append one new `TEST_CASE` under `[otto-host-render][processor-embed]`. Existing 6 cases preserved as regression baseline.

**Submodule bump:**
- `external/OTTO` pointer in IDA's index advances from `4cdbad3e` to the new OTTO HEAD that introduces OTTOEngine.

**Critical files (one source of truth per concern):**
- `external/OTTO/cmake/SharedPluginSources.cmake` — owns the shared source list for OTTOPlugin AND OTTOEngine.
- `external/OTTO/src/otto-engine/CMakeLists.txt` — owns OTTOEngine's build recipe.
- `otto-bridge/src/OttoHost.cpp` — owns the IDA-side embed.
- `tests/OttoHostRenderTests.cpp` — owns the regression baseline.

---

## Task 0 — Copy the plan into the in-tree canonical location

**Files:**
- Create: `docs/superpowers/plans/2026-05-27-otto-engine-embed.md`

- [ ] **Step 1:** Copy this plan to its in-tree location.

```bash
cp /Users/larryseyer/.claude/plans/read-continue-md-read-shiny-elephant.md \
   /Users/larryseyer/IDA/docs/superpowers/plans/2026-05-27-otto-engine-embed.md
```

The in-tree file is the durable artifact and lands in the same atomic commit as the S1 code change.

---

## Task 1 — OTTO-side: parameterize SharedPluginSources, add OTTOEngine target, push

**Files:**
- Modify: `external/OTTO/cmake/SharedPluginSources.cmake`
- Create: `external/OTTO/src/otto-engine/CMakeLists.txt`
- Modify: `external/OTTO/CMakeLists.txt` (gated optional `add_subdirectory`)
- Modify: `external/OTTO/CROSS_PROJECT_INBOX.md` (append entry)
- Commit + push: `external/OTTO` submodule on `origin/main`

- [ ] **Step 1: Verify OTTO submodule is clean at the current pin**

```bash
git -C external/OTTO status --short
git -C external/OTTO rev-parse HEAD
```

Expected: clean tree (or only WIP from the concurrent TAPECOLOR session — if so, stop and surface). HEAD == `4cdbad3e...` (the pin IDA points at).

- [ ] **Step 2: Fetch + rebase OTTO main against origin before edits**

The concurrent TAPECOLOR session may have pushed. Get current.

```bash
git -C external/OTTO fetch origin main
git -C external/OTTO log --oneline HEAD..origin/main
```

If `origin/main` is ahead, pull-rebase (touched paths don't overlap with TAPECOLOR's):

```bash
git -C external/OTTO pull --rebase origin main
```

If rebase conflicts (only possible on `CROSS_PROJECT_INBOX.md`), keep both entries — never delete TAPECOLOR's.

- [ ] **Step 3: Parameterize `cmake/SharedPluginSources.cmake`**

Edit `external/OTTO/cmake/SharedPluginSources.cmake`. Replace line 8:

```cmake
set(OTTO_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/src/otto-plugin")
```

with:

```cmake
# OTTO_SHARED_SOURCES_ROOT defaults to ${CMAKE_SOURCE_DIR} (OTTO's own builds).
# An external consumer (e.g. IDA's add_subdirectory of src/otto-engine) can
# set this BEFORE including this file to point at OTTO's source root, so the
# absolute paths below resolve correctly even when ${CMAKE_SOURCE_DIR} is the
# consuming project's root rather than OTTO's.
if(NOT DEFINED OTTO_SHARED_SOURCES_ROOT)
    set(OTTO_SHARED_SOURCES_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(OTTO_PLUGIN_DIR "${OTTO_SHARED_SOURCES_ROOT}/src/otto-plugin")
```

Then in the same file, replace every remaining occurrence of `${CMAKE_SOURCE_DIR}/src/` with `${OTTO_SHARED_SOURCES_ROOT}/src/`. The hits are on lines 33, 36, 39, 40, 41, 42, 43, 44, 45, 48, 54, 55, 56, 57, 60, 61, 64, 67, 70, 76, 81 (per the OTTO-side Explore output). Do this with sed:

```bash
sed -i.bak 's|${CMAKE_SOURCE_DIR}/src/|${OTTO_SHARED_SOURCES_ROOT}/src/|g' \
    external/OTTO/cmake/SharedPluginSources.cmake
rm external/OTTO/cmake/SharedPluginSources.cmake.bak
```

Verify:

```bash
grep -n 'CMAKE_SOURCE_DIR' external/OTTO/cmake/SharedPluginSources.cmake
```

Expected: zero hits in the source-set blocks. (Any remaining hits in the `juce_add_binary_data` or other non-source contexts are NOT in this file — that file is the otto-plugin CMakeLists.)

- [ ] **Step 4: Create the OTTOEngine target directory + CMakeLists**

```bash
mkdir -p external/OTTO/src/otto-engine
```

Create `external/OTTO/src/otto-engine/CMakeLists.txt`:

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
#
# Today's only consumer is IDA (external/OTTO is a submodule there, and IDA's
# OttoHost embeds an OTTOProcessor instance through this library).
# =============================================================================

# Compute OTTO's source root relative to this CMakeLists. add_subdirectory()
# from another project sets CMAKE_SOURCE_DIR to that project's root, so we
# can't rely on it — we walk up two directories from the engine CMakeLists.
get_filename_component(OTTO_ENGINE_OTTO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Tell SharedPluginSources.cmake to resolve absolute source paths against
# OTTO's tree, not the calling project's tree.
set(OTTO_SHARED_SOURCES_ROOT "${OTTO_ENGINE_OTTO_ROOT}")
include("${OTTO_ENGINE_OTTO_ROOT}/cmake/SharedPluginSources.cmake")

add_library(OTTOEngine STATIC
    ${OTTO_SHARED_PLUGIN_SOURCES}
    ${OTTO_SHARED_CORE_SOURCES}
    ${OTTO_SHARED_UI_SOURCES})

add_library(otto::engine ALIAS OTTOEngine)

# Consumers include <PluginProcessor.h> directly — that header lives in
# src/otto-plugin/ and is exposed PUBLIC so the include path propagates.
target_include_directories(OTTOEngine
    PUBLIC
        "${OTTO_ENGINE_OTTO_ROOT}/src/otto-plugin")

# JUCE modules + Ableton::Link: identical link set to OTTOPlugin (see
# src/otto-plugin/CMakeLists.txt lines 190-213). otto-core comes through
# automatically as a transitive of the shared core sources, but we name it
# explicitly for link-order clarity. lsfx::lsfx_tapecolor is OTTO's shared
# FX dependency.
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

target_compile_features(OTTOEngine PUBLIC cxx_std_20)

# JucePlugin_Name is referenced by PluginProcessor.h (getName() override).
# JUCE_STANDALONE_APPLICATION=0 disables Standalone-only code paths in the
# shared sources. OTTO_EMBEDDED_BUILD=1 is a marker for any future shared-code
# branch that needs to distinguish the embedded library form from the plugin
# or standalone forms; reserved.
target_compile_definitions(OTTOEngine
    PUBLIC
        JucePlugin_Name="OTTO"
        JUCE_STANDALONE_APPLICATION=0
        OTTO_EMBEDDED_BUILD=1)

message(STATUS "OTTOEngine configured (root: ${OTTO_ENGINE_OTTO_ROOT})")
```

Note on `OTTO_SHARED_UI_SOURCES`: SharedPluginSources defines three source variables (plugin, core, UI). OTTOPlugin links all three via `target_sources(OTTOPlugin PRIVATE ${OTTO_SHARED_PLUGIN_SOURCES} ${OTTO_SHARED_CORE_SOURCES} ${OTTO_SHARED_UI_SOURCES})` at otto-plugin/CMakeLists.txt:98-102. OTTOEngine must do the same — the spec's draft at §5.2 omitted UI sources, but the shared code references UI types (PluginEditor includes the UI components) so linking without UI sources would fail at the first undefined-symbol. Confirm by inspection after Step 5's build.

- [ ] **Step 5: Append the optional gated add_subdirectory to OTTO's top-level**

Edit `external/OTTO/CMakeLists.txt`. After line 94 (`add_subdirectory(src/otto-plugin)`), insert:

```cmake
# Optional embedded-engine build. Default OFF so OTTO's own builds (Standalone,
# VST3, AU, CLAP, AUv3 via otto-plugin) are byte-identical. IDA's build does
# NOT enable this option; it add_subdirectory's src/otto-engine directly,
# bypassing OTTO's top-level config dependencies (OTTOConfig.cmake etc.).
# This option exists only for OTTO-side CI verification of the embedded shape.
option(OTTO_BUILD_EMBEDDED_ENGINE
    "Build OTTOEngine static library (for embedding inside another C++/JUCE app)" OFF)

if(OTTO_BUILD_EMBEDDED_ENGINE)
    add_subdirectory(src/otto-engine)
endif()
```

- [ ] **Step 6: Append the inbox entry**

Edit `external/OTTO/CROSS_PROJECT_INBOX.md`. Find the `## [FROM OTTO → IDA]` line near the bottom and insert the new entry immediately ABOVE it. Use this verbatim:

```markdown
### 2026-05-27 — IMPLEMENTATION: OTTOEngine static-library target for IDA embedding (Option D)

Direction: IDA → OTTO
IDA commit: (filled at IDA-side atomic commit time — see Task 5)
OTTO commit: (filled at this Task 1 commit below)
Files touched:
- cmake/SharedPluginSources.cmake (parameterize root via OTTO_SHARED_SOURCES_ROOT; backward-compatible default keeps OTTO's own build unchanged)
- src/otto-engine/CMakeLists.txt (new — STATIC library wrapping the shared plugin sources without plugin-format SDK dependencies)
- CMakeLists.txt (gated add_subdirectory(src/otto-engine) under new OTTO_BUILD_EMBEDDED_ENGINE option, default OFF)

Why: IDA's S1 slice needs OTTOProcessor's full processBlock pipeline embedded in-process. None of OTTO's five existing build shapes fit (Standalone is an executable; VST3/AU/CLAP/AUv3 defer transport to the host). OTTOEngine is the sixth shape — a STATIC library version of the shared plugin code with self-driven transport. Full design + decision log in IDA at docs/superpowers/specs/2026-05-27-otto-engine-static-target.md.

For OTTO's Claude: do NOT revert. OTTOPlugin and its five format targets are byte-identically untouched — OTTOEngine is purely additive. SharedPluginSources.cmake's parameterization is backward-compatible (OTTO_SHARED_SOURCES_ROOT defaults to ${CMAKE_SOURCE_DIR} which is exactly what OTTO's own top-level configure leaves it as). When refactoring shared plugin sources in the future, both OTTOPlugin and OTTOEngine pick up the changes automatically; no second-target maintenance burden. The OTTO_BUILD_EMBEDDED_ENGINE option defaults OFF, so OTTO's own builds are unchanged unless explicitly enabled.

Status: needs-ack
Resolution: (filled when OTTO's next session reviews)
```

- [ ] **Step 7: Commit OTTO + push**

```bash
cd external/OTTO
git add cmake/SharedPluginSources.cmake src/otto-engine/CMakeLists.txt CMakeLists.txt CROSS_PROJECT_INBOX.md
git status --short
```

Expected:
```
M  CMakeLists.txt
M  CROSS_PROJECT_INBOX.md
M  cmake/SharedPluginSources.cmake
A  src/otto-engine/CMakeLists.txt
```

Commit (single-line per user-level CLAUDE.md):

```bash
git commit -m "feat: OTTOEngine static-library target for in-process embedding (Ida-Origin: pending — sixth OTTO build shape per IDA's docs/superpowers/specs/2026-05-27-otto-engine-static-target.md Option D; new src/otto-engine/CMakeLists.txt defines STATIC lib alias otto::engine consuming the same shared plugin/core/UI sources OTTOPlugin uses via paramaterized SharedPluginSources.cmake; same JUCE module link set as OTTOPlugin without plugin-format SDK wrappers or asset bundle; PUBLIC include dir src/otto-plugin/ + PUBLIC compile defs JucePlugin_Name=OTTO + JUCE_STANDALONE_APPLICATION=0 + OTTO_EMBEDDED_BUILD=1; optional gated add_subdirectory(src/otto-engine) under OTTO_BUILD_EMBEDDED_ENGINE option default OFF so OTTO's existing 5 build shapes are byte-identically unaffected; OTTOPlugin unchanged; SharedPluginSources.cmake parameterization is backward-compatible — OTTO_SHARED_SOURCES_ROOT defaults to CMAKE_SOURCE_DIR; consumed by IDA via add_subdirectory(otto-engine) bypassing OTTO's top-level config dependencies; unblocks IDA's S1 OttoHost-embeds-OTTOProcessor work)"
```

Push:

```bash
git pull --rebase origin main   # guard against TAPECOLOR session pushing in the meantime
git push origin main
git rev-parse HEAD              # capture for Task 2 + the IDA commit message
cd -
```

Expected: push succeeds. Save the new OTTO HEAD SHA — call it `NEW_OTTO_SHA`.

The `Ida-Origin: pending` trailer marker is fine here. The matching IDA commit (Task 5) will name this OTTO SHA in its message, and `git -C external/OTTO log --grep='Ida-Origin'` will surface the linkage forever via the inbox entry's IDA-commit reference.

---

## Task 2 — IDA-side: wire OTTOEngine into Dependencies.cmake; verify it configures

**Files:**
- Modify: `cmake/Dependencies.cmake:233` (append OTTOEngine block after the otto-core block)
- Modify: `external/OTTO` submodule pointer (staged but not committed yet — atomic with Task 5)

- [ ] **Step 1: Stage the submodule bump**

```bash
git add external/OTTO
git status --short
```

Expected (sfizz's `m external/sfizz` line may also appear — unrelated, leave it):
```
M  external/OTTO
```

Do NOT commit yet — atomic with Task 5.

- [ ] **Step 2: Append OTTOEngine block to cmake/Dependencies.cmake**

Edit `cmake/Dependencies.cmake`. After line 233 (`message(STATUS "otto-core configured from: ${OTTO_CORE_PATH}")`), append:

```cmake

# -----------------------------------------------------------------------------
# OTTOEngine — OTTO's audio engine as a STATIC library. Provides
# OTTOProcessor (the juce::AudioProcessor subclass) and its full transitive
# dependency chain (Conductor, Pattern engine, MIDI dispatch, sampler voices,
# internal FX, GlobalMixer, self-driven TransportTracker). IDA's OttoHost
# embeds one instance and drives its processBlock per audio buffer.
#
# We add_subdirectory(src/otto-engine) directly rather than going through
# OTTO's top-level CMakeLists, so OTTO's OTTO_* variable surface + the
# pre-existing missing OTTOConfig.cmake reference + plugin-format bundle
# generation are all bypassed. OTTOEngine's CMakeLists is self-contained
# and computes its own paths via CMAKE_CURRENT_SOURCE_DIR. See
# docs/superpowers/specs/2026-05-27-otto-engine-static-target.md.
# -----------------------------------------------------------------------------
set(OTTO_ENGINE_PATH "${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-engine")

if(NOT EXISTS "${OTTO_ENGINE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "otto-engine not found at ${OTTO_ENGINE_PATH}. "
        "Submodule may be stale — run: git submodule update --init --recursive")
endif()

add_subdirectory("${OTTO_ENGINE_PATH}" "${CMAKE_BINARY_DIR}/otto-engine")
message(STATUS "otto-engine configured from: ${OTTO_ENGINE_PATH}")
```

- [ ] **Step 3: Clean configure to catch CMake-graph errors immediately**

Per `feedback_clean_builds_only_for_testing` — any operator-test handoff requires a clean build, and this is a structural CMake change so we configure clean:

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -30
```

Expected: configure succeeds. Look for `-- OTTOEngine configured (root: …/external/OTTO)` and `-- otto-engine configured from: …/external/OTTO/src/otto-engine` STATUS lines.

If configure fails:
- "Cannot find source file" referencing an `OTTO_SHARED_SOURCES_ROOT` path → Task 1 Step 3's sed didn't catch every `${CMAKE_SOURCE_DIR}` hit in `SharedPluginSources.cmake`. Re-grep and fix.
- "Unknown CMake command target_link_libraries" or similar early-eval failure → OTTOEngine is being parsed before juce_add_plugin has run for the JUCE module aliases. Verify our `add_subdirectory(otto-engine)` lands AFTER `add_subdirectory(otto-core)` (which pulls in JUCE module setup transitively).
- "lsfx::lsfx_tapecolor not found" → the lsfx_tapecolor target must already exist when otto-engine configures. Check that `cmake/Dependencies.cmake` configures lsfx_tapecolor BEFORE the otto-engine block (it should — lsfx_tapecolor is added higher up in Dependencies.cmake).
- "Ableton::Link not found" → same as above, Ableton::Link must configure before otto-engine.

- [ ] **Step 4: Build OTTOEngine in isolation to confirm it compiles**

```bash
cmake --build build --target OTTOEngine 2>&1 | tail -30
```

Expected: clean build of the static library. Resulting artifact at `build/otto-engine/libOTTOEngine.a`. Most likely failure mode: missing `JucePlugin_*` macro reference in some shared source file (the spec at §5.2 names `JucePlugin_Name` as the only one the shared code currently references via `PluginProcessor.h:70`). If new `JucePlugin_*` references surface, add them to OTTOEngine's PUBLIC `target_compile_definitions` block and re-build. This is the expected long-tail maintenance per spec §5.2.

---

## Task 3 — IDA-side: link otto::engine into otto-bridge + refactor OttoHost.cpp

**Files:**
- Modify: `otto-bridge/CMakeLists.txt:31-42` (add `otto::engine` + `juce::juce_audio_processors` to PRIVATE link list)
- Modify: `otto-bridge/src/OttoHost.cpp` (substantial refactor of Impl + prepare + isPrepared + renderBlock + both accessors)
- UNCHANGED: `otto-bridge/include/ida/OttoHost.h` (verify byte-identical after refactor)

- [ ] **Step 1: Update `otto-bridge/CMakeLists.txt` link list**

Edit `otto-bridge/CMakeLists.txt`. Replace lines 31-42 (the `target_link_libraries(IdaOttoBridge ...)` block) with:

```cmake
# M-OTTO-4 slice 2: OttoHost inherits from `ida::IOttoRenderSource` (audio-thread
# port driven by the AudioCallback). That interface lives in `core/` so
# AudioCallback can name it without depending on OttoBridge. Ida::Core moves
# from transitive-PRIVATE (via Ida::Engine) to PUBLIC on IdaOttoBridge —
# consumers of OttoHost.h need the IOttoRenderSource header on their include
# path.
#
# S1 (2026-05-27): otto::engine replaces the prior otto::core-only link.
# OTTOEngine is OTTO's static-library shape providing OTTOProcessor (the
# juce::AudioProcessor subclass we now embed inside Impl). otto-engine
# transitively pulls otto-core, every JUCE module OTTOPlugin uses, and
# Ableton::Link — we keep the explicit otto::core + juce::juce_audio_basics
# + juce::juce_events + lsfx::lsfx_tapecolor lines for link-order clarity
# and to document the bridge's first-party dependencies. juce_audio_processors
# is new: OTTOProcessor inherits juce::AudioProcessor which our .cpp now names
# directly.
target_link_libraries(IdaOttoBridge
    PUBLIC  Ida::Core
    PRIVATE otto::core
            otto::engine
            juce::juce_audio_basics
            juce::juce_audio_processors
            juce::juce_events
            lsfx::lsfx_tapecolor
            Ida::Engine)
```

No include-path plumbing needed: OTTOEngine exports `src/otto-plugin/` PUBLIC, so `#include <PluginProcessor.h>` from `OttoHost.cpp` resolves transitively through the `otto::engine` link.

- [ ] **Step 2: Update OttoHost.cpp includes**

Edit `otto-bridge/src/OttoHost.cpp`. Replace the include block at lines 6-10:

```cpp
#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/transport/TransportTracker.h>

#include <juce_events/juce_events.h>
```

with:

```cpp
#include <otto/common/EventBus.h>
#include <otto/manager/PlayerManager.h>
#include <otto/mixer/GlobalMixer.h>
#include <otto/transport/TransportTracker.h>

#include <PluginProcessor.h>   // OTTOProcessor — the juce::AudioProcessor we embed

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
```

Rationale: `OTTOProcessor` lives in the global namespace per OTTO's plugin convention (declared at `external/OTTO/src/otto-plugin/PluginProcessor.h` line ~75 — verified by the explore: `getPlayerManager()` exists at lines 361-362 inside `class OTTOProcessor`). `juce_audio_basics` for `AudioBuffer<float>` + `MidiBuffer`. `juce_audio_processors` for `juce::AudioProcessor` base. `EventBus` + `PlayerManager` + `GlobalMixer` + `TransportTracker` includes stay — still referenced via OTTOProcessor accessor return types and the existing EventBus subscription.

- [ ] **Step 3: Refactor the Impl struct**

Edit `otto-bridge/src/OttoHost.cpp`. Replace the entire `struct OttoHost::Impl` definition (lines 47-118) with:

```cpp
struct OttoHost::Impl : public juce::Timer
{
    Impl()
        : transportRing (kTransportRingCapacity)
        , processor     (std::make_unique<OTTOProcessor>())
    {
        // Subscription to OTTO's transport EventBus. Path unchanged from
        // M-OTTO-3: OTTO's audio thread publishes TransportEvent →
        // [audio-thread RT-safe lambda → SPSC ring] → message-thread Timer
        // drainer → listener fan-out. The S1 embed does NOT alter this:
        // OTTOProcessor's internal TransportTracker still publishes to the
        // same singleton EventBus the subscription reads from.
        subscription = ::otto::EventBus::instance().subscribe<::otto::TransportEvent> (
            [this] (const ::otto::TransportEvent& event) noexcept
            {
                TransportSnapshot snap;
                snap.kind       = translateKind (event.type);
                snap.bpm        = event.state.bpm;
                snap.isPlaying  = event.state.isPlaying;
                snap.timeSigNum = event.state.timeSignature.numerator;
                snap.timeSigDen = event.state.timeSignature.denominator;
                (void) transportRing.push (snap);
            });

        startTimerHz (kDrainTimerHz);
    }

    ~Impl() override
    {
        // Stop drain timer first — no further listener callbacks fire while
        // we tear down. Then member destruction runs in reverse declaration
        // order: subscription (declared last) → unsubscribes from the bus,
        // serialising against any in-flight publish; then processor → OTTO
        // tears down; then everything else.
        stopTimer();
    }

    void timerCallback() override { drain(); }

    void drain()
    {
        TransportSnapshot snap;
        while (transportRing.pop (snap))
        {
            // Listeners can be added/removed from inside a callback; iterate
            // over a local copy so mutation during fan-out doesn't invalidate
            // the iterator. Message-thread only — allocation here is fine.
            const auto snapshot = listeners;
            for (auto* listener : snapshot)
                if (listener != nullptr)
                    listener->onOttoTransport (snap);
        }
    }

    // OTTOProcessor IS a juce::AudioProcessor — prepareToPlay + processBlock
    // run OTTO's full pipeline (Conductor + Pattern engine + MIDI dispatch +
    // sampler voices + internal FX + GlobalMixer). The 32 stereo per-output
    // buffers are populated inside processor->getPlayerManager().getGlobalMixer()
    // as a side effect of processBlock and read out via getOttoOutputLeft/Right.
    //
    // Declaration order matters:
    //   processor   — declared before subscription so it destroys AFTER
    //                 subscription, ensuring no EventBus callback can race
    //                 the processor's teardown.
    //   subscription — declared LAST so it destroys FIRST (after the timer
    //                  has been stopped in the dtor body).
    std::unique_ptr<OTTOProcessor> processor;

    // Scratch storage for processBlock. processor->processBlock requires a
    // juce::AudioBuffer<float> + juce::MidiBuffer; OTTO populates the buffer
    // per its OutputRouter::Mode (default Stereo → channels 0-1 only), and
    // IDA reads the 32 per-output stereo buffers from GlobalMixer instead
    // (processBlock populates them as a side effect). The view's contents
    // are discarded. Sized once at prepare time; no per-block allocation.
    juce::AudioBuffer<float> scratchBuffer;
    juce::MidiBuffer         scratchMidi;
    int                      cachedMaxBlock { 0 };
    bool                     prepared       { false };

    LockFreeSpscQueue<TransportSnapshot> transportRing;
    std::vector<IOttoTransportListener*> listeners;       // message-thread only

    // Declared LAST → destroyed FIRST (after stopTimer()). SubscriptionHandle
    // dtor unsubscribes from the singleton bus and serialises against any
    // in-flight publish.
    ::otto::SubscriptionHandle subscription;
};
```

- [ ] **Step 4: Rewrite `OttoHost::prepare` + `OttoHost::isPrepared`**

In the same file, replace lines 127-133 (the existing `prepare` + `isPrepared` definitions) with:

```cpp
void OttoHost::prepare (double sampleRate, int maxBlockSize)
{
    // Allocate scratch buffer ONCE at the maxBlockSize ceiling. Subsequent
    // renderBlock calls build a non-owning AudioBuffer<float> view sized to
    // the actual numSamples — zero per-block allocation on the audio thread.
    impl_->scratchBuffer.setSize (2, maxBlockSize,
                                  /*keepExistingContent=*/false,
                                  /*clearExtraSpace=*/true,
                                  /*avoidReallocating=*/false);
    impl_->scratchMidi.ensureSize (256);  // headroom for one block of MIDI hits
    impl_->cachedMaxBlock = maxBlockSize;

    // Drive OTTO's AudioProcessor prepareToPlay — propagates internally to
    // PlayerManager::prepare (the original direct call site) AND to every
    // other subsystem OTTOProcessor owns (Conductor, Pattern engine, internal
    // FX, TransportTracker).
    impl_->processor->prepareToPlay (sampleRate, maxBlockSize);
    impl_->prepared = true;
}

bool OttoHost::isPrepared() const noexcept
{
    return impl_->prepared;
}
```

- [ ] **Step 5: Rewrite `OttoHost::renderBlock`**

Replace the existing `renderBlock` definition (currently around lines 157-165) with:

```cpp
void OttoHost::renderBlock (int numSamples) noexcept
{
    if (! impl_->prepared || numSamples <= 0 || numSamples > impl_->cachedMaxBlock)
        return;

    // Non-owning view into the pre-allocated scratch buffer's first
    // numSamples samples. The (channel-pointers, channels, samples) ctor
    // does not allocate — just a header wrapper. OTTO's processBlock writes
    // its routed output into this view per OutputRouter::Mode (we discard
    // it); the side effect we care about is GlobalMixer being populated with
    // the 32 per-output stereo pairs, which the accessors below read.
    float* const channelPtrs[2] = {
        impl_->scratchBuffer.getWritePointer (0),
        impl_->scratchBuffer.getWritePointer (1)
    };
    juce::AudioBuffer<float> view (channelPtrs, 2, numSamples);

    // OTTO may add to the MIDI buffer (drum hits routed to outboard MIDI);
    // clear before each call so it doesn't accumulate. clear() does not
    // allocate — ensureSize was called once in prepare.
    impl_->scratchMidi.clear();

    impl_->processor->processBlock (view, impl_->scratchMidi);
}
```

- [ ] **Step 6: Rewrite the two `getOttoOutput{Left,Right}` accessors**

Replace the existing accessor definitions (currently around lines 167-213 in the file) with:

```cpp
const float* OttoHost::getOttoOutputLeft (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->prepared)
        return nullptr;

    const auto& mixer = impl_->processor->getPlayerManager().getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputLeft (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputLeft (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputLeft (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}

const float* OttoHost::getOttoOutputRight (int ottoOutputIndex) const noexcept
{
    if (ottoOutputIndex < 0 || ottoOutputIndex >= kNumOttoOutputs)
        return nullptr;
    if (! impl_->prepared)
        return nullptr;

    const auto& mixer = impl_->processor->getPlayerManager().getGlobalMixer();
    if (ottoOutputIndex < kOttoFxReturnRangeBegin)
        return mixer.getChannelOutputRight (ottoOutputIndex - kOttoChannelRangeBegin);
    if (ottoOutputIndex < kOttoPlayerBusRangeBegin)
        return mixer.getFxReturnOutputRight (ottoOutputIndex - kOttoFxReturnRangeBegin);
    return mixer.getPlayerOutputRight (ottoOutputIndex - kOttoPlayerBusRangeBegin);
}
```

Diff vs. the pre-S1 version: the GlobalMixer access path changed from `impl_->playerManager.getGlobalMixer()` to `impl_->processor->getPlayerManager().getGlobalMixer()`. Range-dispatch logic is byte-identical.

- [ ] **Step 7: Verify the public header is untouched**

```bash
git diff -- otto-bridge/include/ida/OttoHost.h
```

Expected: empty. The slice-4b external API shape is preserved byte-identical (per spec §6.6).

- [ ] **Step 8: Build IdaOttoBridge in isolation to catch compile errors fast**

```bash
cmake --build build --target IdaOttoBridge 2>&1 | tail -30
```

Expected: clean build.

Most likely failure modes:
- `'PluginProcessor.h' file not found` → OTTOEngine's PUBLIC include dir didn't propagate. Confirm `target_include_directories(OTTOEngine PUBLIC ...)` in Task 1 Step 4 lists `${OTTO_ENGINE_OTTO_ROOT}/src/otto-plugin`.
- `unknown type name 'OTTOProcessor'` → header include resolved but the class declaration is missing. The class is in the GLOBAL namespace (not `otto::`); confirm `std::unique_ptr<OTTOProcessor>` (no namespace) in our `Impl`.
- `'getPlayerManager' is a private member` → false alarm; the accessor at PluginProcessor.h:361-362 is public. Re-read the file.
- `undefined reference to OTTOProcessor::...` at link time → `otto::engine` library didn't build. Re-run Task 2 Step 4.
- `-Wreorder` warning treated as error → confirm Impl member declaration order matches initializer list order: `transportRing` first (initialized first), then `processor`, then everything else, with `subscription` LAST.

---

## Task 4 — Add the embed-pin regression test + run the full suite

**Files:**
- Modify: `tests/OttoHostRenderTests.cpp` (append one new `TEST_CASE`)

- [ ] **Step 1: Append the new regression-pin test**

Edit `tests/OttoHostRenderTests.cpp`. After the existing closing `}` of the last `TEST_CASE` (`"OttoHost::renderBlock tolerates zero / negative numSamples without crashing"`), append:

```cpp
TEST_CASE ("OttoHost embeds OTTOProcessor — full processBlock path is driven",
           "[otto-host-render][processor-embed]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    OttoHost host;
    host.prepare (kTestSampleRate, kTestBlockSize);
    REQUIRE (host.isPrepared());

    // After prepare, every in-range accessor returns non-null even before
    // any renderBlock — GlobalMixer's per-channel storage is allocated by
    // PlayerManager::prepare (which OTTOProcessor::prepareToPlay transitively
    // calls).
    for (int i = 0; i < OttoHost::kNumOttoOutputs; ++i)
    {
        CHECK (host.getOttoOutputLeft  (i) != nullptr);
        CHECK (host.getOttoOutputRight (i) != nullptr);
    }

    // Many processBlock calls — the OTTOProcessor embed must survive
    // sustained per-block driving without crashing, leaking, or destabilizing
    // GlobalMixer's per-channel buffer pointers (OTTO's GlobalMixer allocates
    // its buffers once in prepare() and reuses them). RT-safety of
    // OTTOProcessor::processBlock is OTTO's own contract per its CLAUDE.md
    // AUDIO THREAD RULES; this test pins the IDA-side WIRING, not the
    // OTTO-side invariants.
    const float* l0_first   = host.getOttoOutputLeft  (0);
    const float* lFx_first  = host.getOttoOutputLeft  (OttoHost::kOttoFxReturnRangeBegin);
    const float* rBus_first = host.getOttoOutputRight (OttoHost::kOttoPlayerBusRangeBegin);

    for (int block = 0; block < 100; ++block)
        host.renderBlock (kTestBlockSize);

    CHECK (host.getOttoOutputLeft  (0)                                == l0_first);
    CHECK (host.getOttoOutputLeft  (OttoHost::kOttoFxReturnRangeBegin)  == lFx_first);
    CHECK (host.getOttoOutputRight (OttoHost::kOttoPlayerBusRangeBegin) == rBus_first);
}
```

- [ ] **Step 2: Build IdaTests**

```bash
cmake --build build --target IdaTests 2>&1 | tail -30
```

Expected: clean build. If link fails with `undefined symbol OTTOProcessor::*`, the test target may need `otto::engine` directly. Check `tests/CMakeLists.txt` near the existing OTTO test target line and add `otto::engine` to its PRIVATE link if so. (Most likely not needed — the test exercises OttoHost only, which already links otto::engine via IdaOttoBridge.)

- [ ] **Step 3: Run the OttoHost test tags to confirm baseline + new test**

```bash
cd build && ctest --output-on-failure -R "OttoHost" 2>&1 | tail -30 ; cd ..
```

Expected: all 7 render cases (6 baseline + 1 new processor-embed) + all 6 transport cases pass.

Most likely failure modes for the new test:
- `getOttoOutputLeft(0) == nullptr` after prepare → `processor->prepareToPlay` didn't allocate the GlobalMixer buffers. Check OTTO's PlayerManager::prepare is being called — set a breakpoint or add a one-shot DBG line in OTTO (then remove before commit).
- Pointer instability → OTTO's GlobalMixer is reallocating per-block, which would be an OTTO regression unrelated to S1. Confirm against `git -C external/OTTO log` since the previous SHA — only OTTOEngine CMake added, no GlobalMixer changes.
- Test crashes inside `OTTOProcessor` ctor → OTTO is hitting an asset-loading code path that expects asset files present at startup. Confirm `OTTO_ASSETS_DIR` points at the operator's OTTO checkout per `~/.claude/CLAUDE.md` "iOS / JUCE Platform Rules"; the test exe needs to find those assets.

- [ ] **Step 4: Run the full ctest suite**

```bash
ctest --test-dir build 2>&1 | tail -10
```

Expected: **790 passed, 1 not-run** — the pre-S1 baseline (per continue.md). The 1 not-run is `MainComponentPluginEditorTests` (separately built; runs via `bash bash/test-s7.sh`).

If any test that previously passed now fails: stop, do not commit. Diagnose. S1 should not introduce user-visible behavioral change.

- [ ] **Step 5: Verify the IDA app builds + links**

```bash
cmake --build build --target IDA 2>&1 | tail -10
```

Expected: clean build of `build/app/IDA_artefacts/Release/IDA.app`. MainComponent wiring at `app/MainComponent.cpp:4274` (`ottoHost_ = std::make_unique<ida::OttoHost>();`) + `:4281` (`audioCallback_->setOttoRenderSource (ottoHost_.get());`) is untouched and continues to compile against the byte-identical OttoHost.h.

---

## Task 5 — Atomic IDA commit (otto-bridge + Dependencies + submodule bump + test + plan + spec)

**Files staged in one commit:**
- `cmake/Dependencies.cmake`
- `otto-bridge/CMakeLists.txt`
- `otto-bridge/src/OttoHost.cpp`
- `tests/OttoHostRenderTests.cpp`
- `external/OTTO` (submodule pointer bump to NEW_OTTO_SHA from Task 1 Step 7)
- `docs/superpowers/plans/2026-05-27-otto-engine-embed.md` (the in-tree plan, created in Task 0)

- [ ] **Step 1: Stage**

```bash
git add cmake/Dependencies.cmake \
        otto-bridge/CMakeLists.txt \
        otto-bridge/src/OttoHost.cpp \
        tests/OttoHostRenderTests.cpp \
        external/OTTO \
        docs/superpowers/plans/2026-05-27-otto-engine-embed.md
git status --short
```

Expected (with possible additional unrelated `m external/sfizz`):
```
M  cmake/Dependencies.cmake
M  external/OTTO
M  otto-bridge/CMakeLists.txt
M  otto-bridge/src/OttoHost.cpp
A  docs/superpowers/plans/2026-05-27-otto-engine-embed.md
M  tests/OttoHostRenderTests.cpp
```

- [ ] **Step 2: Capture the new OTTO SHA + commit**

```bash
NEW_OTTO_SHA=$(git -C external/OTTO rev-parse --short HEAD)
echo "Bumping OTTO submodule to ${NEW_OTTO_SHA}"
```

Commit with a single-line message:

```bash
git commit -m "feat: S1 — OttoHost embeds OTTOProcessor via OTTOEngine static lib (replaces bare PlayerManager+TransportTracker pair with full juce::AudioProcessor embed; OTTO's Conductor + Pattern engine + MIDI dispatch + internal FX + GlobalMixer + self-driven TransportTracker now drive inside OttoHost::renderBlock; prepare allocates scratch AudioBuffer + MidiBuffer once at maxBlockSize, renderBlock builds a non-owning view sized to numSamples and calls processor->processBlock; per-output accessors keep their shape by reading through processor->getPlayerManager().getGlobalMixer() (the accessor pair at PluginProcessor.h:361-362 was already present — original plan's T1 to add it was a no-op); transport-listener fan-out via the singleton EventBus subscription is unaffected since OTTOProcessor's internal TransportTracker publishes to the same bus; OTTO submodule bumped to ${NEW_OTTO_SHA} for the new OTTOEngine sixth-build-shape target (alias otto::engine — STATIC library wrapping the shared plugin sources without plugin-format SDK or app wrapper or asset bundle; consumes the same paramaterized SharedPluginSources.cmake OTTOPlugin uses, preserving single source of truth; PUBLIC include dir src/otto-plugin/ + PUBLIC compile defs JucePlugin_Name=OTTO + JUCE_STANDALONE_APPLICATION=0 + OTTO_EMBEDDED_BUILD=1 — full design in docs/superpowers/specs/2026-05-27-otto-engine-static-target.md); cmake/Dependencies.cmake gains add_subdirectory(external/OTTO/src/otto-engine) after the existing otto-core block (bypasses OTTO's top-level CMakeLists, avoiding the pre-existing missing OTTOConfig.cmake reference + plugin-format bundle generation IDA does not want); otto-bridge/CMakeLists.txt PRIVATE-links otto::engine + juce::juce_audio_processors (OTTOEngine's PUBLIC include dir propagation removes the need for IDA-side JucePlugin_Name workaround that the failed first attempt added); tests/OttoHostRenderTests.cpp gains one [otto-host-render][processor-embed] regression-pin TEST_CASE; existing 6 [otto-host-render] + 6 [otto-host-transport] cases preserved as baseline; ctest 790/791 baseline preserved; public OttoHost.h byte-identical so MainComponent + AudioCallback wiring at app/MainComponent.cpp:4274/4281 is untouched; S2 OttoPane audibility unblocked since OttoPane can now call processor->createEditor() once the tab lands; supersedes plans/2026-05-27-otto-host-embeds-ottoprocessor.md which was invalidated by a CMake-graph gap the original plan author didn't verify)"
```

- [ ] **Step 3: Push IDA**

Per `feedback_claude_commits_and_pushes_master`:

```bash
git push origin master
git log -1 --oneline
```

Expected: push succeeds. New S1 commit at HEAD.

---

## Verification (end-to-end S1 acceptance)

After Task 5 lands, ALL of the following must hold. Per `feedback_clean_builds_only_for_testing`, any operator-test handoff that follows runs against a clean rebuild.

- [ ] **Engine acceptance.** `ctest --test-dir build` reports **790 passed, 1 not-run** — the pre-S1 baseline preserved exactly. The new `[otto-host-render][processor-embed]` case is included in the pass count.
- [ ] **Public-surface preservation.** `git diff 924836d..HEAD -- otto-bridge/include/ida/OttoHost.h` returns empty. The slice-4b external API shape is byte-identical.
- [ ] **OTTOEngine artifact exists.** `ls build/otto-engine/libOTTOEngine.a` returns the path. The IdaTests link line shows `libOTTOEngine.a` somewhere in its link inputs (verify via `cmake --build build --target IdaTests --verbose 2>&1 | grep -i ottoengine | head -5` — must show OTTOEngine being linked, NOT a bare `-lOTTOPlugin`).
- [ ] **Submodule bump audit trail.** `git -C external/OTTO log -1 --format='%H %s'` shows the new OTTOEngine commit at OTTO HEAD. `grep "OTTOEngine" external/OTTO/CROSS_PROJECT_INBOX.md` returns the inbox entry with `Status: needs-ack`.
- [ ] **App build.** `cmake --build build --target IDA` succeeds. `build/app/IDA_artefacts/Release/IDA.app` is the updated build.
- [ ] **No operator-visible change.** Launching the app from the Desktop `IDA` alias and clicking "Add OTTO source ▶" in OutputMixerPane still lists the 32 canonical names (Kick / Snare / SideStick / … / PlayerOut1..4) and creating OTTO strips still works as it did pre-S1 — the engine swap is invisible at this layer. (This is the deferred operator-eyes step from spec §8.3; S1 produces no UI change, so it's confirmatory not gating.)

After S1 lands: the next plan target is **S2 — OttoPane tab via `OTTOProcessor::createEditor()`** (closes the M-OTTO-4 audibility gap). Invoke `superpowers:writing-plans` against §7 row S2 of `docs/superpowers/specs/2026-05-27-otto-integration-architecture.md` when ready.

---

## Self-review

**Spec coverage:**
- Spec §5.1 (parameterize SharedPluginSources.cmake) → Task 1 Step 3.
- Spec §5.2 (new src/otto-engine/CMakeLists.txt) → Task 1 Step 4.
- Spec §5.3 (optional top-level add_subdirectory under OTTO_BUILD_EMBEDDED_ENGINE) → Task 1 Step 5.
- Spec §6.1 (cmake/Dependencies.cmake add_subdirectory otto-engine) → Task 2 Step 2.
- Spec §6.2 (otto-bridge/CMakeLists.txt link otto::engine + juce_audio_processors) → Task 3 Step 1.
- Spec §6.3 (OttoHost.cpp embed refactor) → Task 3 Steps 2-6.
- Spec §6.4 (submodule SHA bump) → Task 2 Step 1 + Task 5 Step 1.
- Spec §6.5 (regression-pin test case) → Task 4 Step 1.
- Spec §6.6 (OttoHost.h byte-identical) → Task 3 Step 7 (verify) + Verification.
- Spec §7.1 (cross-project sequencing) → Task 1 push first → Task 2 SHA capture → Task 5 atomic IDA commit + push.
- Spec §7.2 (TAPECOLOR collision protocol) → Task 1 Step 2 (fetch+rebase) + Step 7 (pull-rebase before push).
- Spec §7.3 (inbox entry template) → Task 1 Step 6 verbatim.
- Spec §8 (verification contract) → Verification section enumerates exact commands + expected output.
- Spec §9 (transport authority restated) → respected by NOT feeding a juce::AudioPlayHead anywhere; OTTOProcessor's internal TransportTracker drives.

**Placeholder scan:** no "TBD" / "TODO" / "implement later". Every step has either exact code, exact command + expected output, or exact verification. The two read-then-check steps (Task 3 Step 7 verify-header-untouched + Task 4 Step 3 the count check) are concrete `git diff` / `ctest -R` commands with explicit expected output.

**Type consistency:** `OttoHost::Impl` member names consistent across Task 3 steps (`processor`, `scratchBuffer`, `scratchMidi`, `cachedMaxBlock`, `prepared`, `transportRing`, `listeners`, `subscription`). The `OTTOProcessor` class lives in the global namespace (per OTTO's plugin convention and confirmed by the explore agent's read of PluginProcessor.h:361-362) — used as `std::unique_ptr<OTTOProcessor>` consistently. Member declaration order matches initializer-list order (transportRing → processor → subscription LAST) to avoid `-Wreorder` under `-Werror`. OTTOEngine target alias `otto::engine` named consistently in OTTO-side Task 1 Step 4, IDA-side Task 2 Step 2 (add_subdirectory result), and Task 3 Step 1 (link list).
