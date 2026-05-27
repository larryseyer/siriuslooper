# =============================================================================
# IDA — Third-Party Dependencies
# =============================================================================
# Dependencies are vendored as plain directories under external/ (gitignored,
# not committed) and built via local-path add_subdirectory. This matches the
# sister app OTTO's layout. Populate external/ with bash/setup-deps.sh.
#
# Pinned versions:
#   JUCE    8.0.12
#   Catch2  v3.15.0
#   soxr    0.1.3 (master — the library is effectively frozen at this release)
# =============================================================================

# -----------------------------------------------------------------------------
# JUCE — application and audio framework
# -----------------------------------------------------------------------------
set(JUCE_PATH "${CMAKE_SOURCE_DIR}/external/JUCE")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_PATH}. Run: bash/setup-deps.sh")
endif()

# JUCE_ENABLE_MODULE_SOURCE_GROUPS is OFF deliberately. When ON, JUCE attaches
# every module .cpp/.mm file (not just the wrapper sources) as INTERFACE
# sources of the module library and the juce_add_* helpers then patch them
# with HEADER_FILE_ONLY so they aren't actually compiled. That fixup is not
# applied to plain `add_library` targets that link a JUCE module — the
# persistence layer is one — so the extra files end up in their build set and
# the build fails. The setting only affects IDE source-group display.
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
message(STATUS "JUCE configured from: ${JUCE_PATH}")

# -----------------------------------------------------------------------------
# Catch2 — test framework (development only, not distributed)
# -----------------------------------------------------------------------------
if(IDA_BUILD_TESTS)
    set(CATCH2_PATH "${CMAKE_SOURCE_DIR}/external/Catch2")

    if(NOT EXISTS "${CATCH2_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "Catch2 not found at ${CATCH2_PATH}. Run: bash/setup-deps.sh")
    endif()

    add_subdirectory("${CATCH2_PATH}" "${CMAKE_BINARY_DIR}/Catch2")
    list(APPEND CMAKE_MODULE_PATH "${CATCH2_PATH}/extras")
    message(STATUS "Catch2 configured from: ${CATCH2_PATH}")
endif()

# -----------------------------------------------------------------------------
# libsoxr — continuous async sample-rate conversion at the membranes.
# Statically linked: libsoxr is LGPL-2.1+, which is compatible with IDA's
# AGPLv3 software license, so static linking is permitted.
# -----------------------------------------------------------------------------
set(SOXR_PATH "${CMAKE_SOURCE_DIR}/external/soxr")

if(NOT EXISTS "${SOXR_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "libsoxr not found at ${SOXR_PATH}. Run: bash/setup-deps.sh")
endif()

# soxr declares a very old cmake_minimum_required; allow it under modern CMake,
# and let it keep its old source-file-extension style (CMP0115) without noise.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_POLICY_DEFAULT_CMP0115 OLD)

set(BUILD_TESTS       OFF CACHE BOOL "" FORCE) # soxr's own sanity tests
set(BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(WITH_OPENMP       OFF CACHE BOOL "" FORCE) # avoid an OpenMP toolchain dependency
set(WITH_LSR_BINDINGS OFF CACHE BOOL "" FORCE) # no libsamplerate-compat interface
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory("${SOXR_PATH}" "${CMAKE_BINARY_DIR}/soxr")

# soxr's public header lives in its src/ tree; expose it to consumers of the
# soxr target.
target_include_directories(soxr INTERFACE "${SOXR_PATH}/src")

unset(CMAKE_POLICY_VERSION_MINIMUM)
unset(CMAKE_POLICY_DEFAULT_CMP0115)
message(STATUS "libsoxr configured from: ${SOXR_PATH}")

# -----------------------------------------------------------------------------
# CLAP — plug-in SDK (header-only). Hosted by ida_plugin_host (M7 S2+)
# and built into test fixtures (SyntheticTestPlugin) for round-trip coverage.
# CLAP itself ships only headers, but its CMakeLists exposes a `clap`
# INTERFACE target with the include dir — the cleanest way to consume it.
# -----------------------------------------------------------------------------
set(CLAP_PATH "${CMAKE_SOURCE_DIR}/external/clap")

if(NOT EXISTS "${CLAP_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "CLAP not found at ${CLAP_PATH}. Run: bash/setup-deps.sh")
endif()

# CLAP_BUILD_TESTS spins up its own compile-test executables + a sample
# plug-in target. None of that belongs in our build.
set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${CLAP_PATH}" "${CMAKE_BINARY_DIR}/clap")
message(STATUS "CLAP configured from: ${CLAP_PATH}")

# -----------------------------------------------------------------------------
# lsfx_tapecolor — shared tape-emulation FX module (OTTO + IDA). Owned by
# OTTO at github.com:larryseyer/lsfx_tapecolor (AGPLv3); both projects
# consume the same submodule and OTTO is the canonical pin driver. Per
# the cross-project protocol (CLAUDE.md), do not bump this SHA in IDA
# without checking OTTO's pin first.
# -----------------------------------------------------------------------------
set(LSFX_TAPECOLOR_PATH "${CMAKE_SOURCE_DIR}/external/lsfx_tapecolor")

if(NOT EXISTS "${LSFX_TAPECOLOR_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "lsfx_tapecolor not found at ${LSFX_TAPECOLOR_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory("${LSFX_TAPECOLOR_PATH}" "${CMAKE_BINARY_DIR}/lsfx_tapecolor")
message(STATUS "lsfx_tapecolor configured from: ${LSFX_TAPECOLOR_PATH}")

# -----------------------------------------------------------------------------
# sfizz — SFZ sample-playback library. Required transitively by otto-core
# (its SfizzWrapper.cpp links sfizz::sfizz). Consumed as a recursive
# submodule at external/sfizz/. The 64-channel patch (sfizz defaults to
# maxChannels=32, must be 64 to match OTTO's 32 stereo-pair output
# topology — see external/OTTO/CLAUDE.md) is applied here at configure
# time so the patched Config.h.in is in place before sfizz's add_subdirectory
# generates Config.h.
# -----------------------------------------------------------------------------
set(SFIZZ_PATH "${CMAKE_SOURCE_DIR}/external/sfizz")

if(NOT EXISTS "${SFIZZ_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "sfizz not found at ${SFIZZ_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

set(SFIZZ_CONFIG_TEMPLATE "${SFIZZ_PATH}/src/Config.h.in")
file(READ "${SFIZZ_CONFIG_TEMPLATE}" _sfizz_cfg)
if(_sfizz_cfg MATCHES "maxChannels { 64 }")
    message(STATUS "sfizz: 64-channel patch already applied")
elseif(_sfizz_cfg MATCHES "maxChannels { 32 }")
    message(STATUS "sfizz: applying 64-channel patch")
    execute_process(
        COMMAND patch -p1 -i "${CMAKE_SOURCE_DIR}/patches/sfizz-max-channels.patch"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE _sfizz_patch_rc
        OUTPUT_VARIABLE _sfizz_patch_out
        ERROR_VARIABLE  _sfizz_patch_err)
    if(NOT _sfizz_patch_rc EQUAL 0)
        message(FATAL_ERROR
            "sfizz 64-channel patch failed (rc=${_sfizz_patch_rc}):\n"
            "${_sfizz_patch_out}\n${_sfizz_patch_err}")
    endif()
    # Drop the .orig backup BSD patch leaves behind so sfizz's working tree
    # stays as clean as it can be (the modified Config.h.in itself is the
    # expected, durable diff).
    file(REMOVE "${SFIZZ_CONFIG_TEMPLATE}.orig")
else()
    message(FATAL_ERROR
        "sfizz Config.h.in is in an unexpected state — neither the "
        "stock 'maxChannels { 32 }' nor the patched 'maxChannels { 64 }' "
        "line was found. Check the external/sfizz checkout integrity.")
endif()
unset(_sfizz_cfg)

# sfizz build-option discipline (mirrors OTTO's Dependencies.cmake).
set(SFIZZ_JACK        OFF CACHE BOOL "" FORCE)
set(SFIZZ_RENDER      OFF CACHE BOOL "" FORCE)
set(SFIZZ_BENCHMARKS  OFF CACHE BOOL "" FORCE)
set(SFIZZ_TESTS       OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEMOS       OFF CACHE BOOL "" FORCE)
set(SFIZZ_DEVTOOLS    OFF CACHE BOOL "" FORCE)
set(SFIZZ_SHARED      OFF CACHE BOOL "" FORCE)

# Apple Clang 16+ promotes -Wmissing-template-arg-list-after-template-kw to
# error; sfizz's atomic_queue.h uses the older form. Quiet it for sfizz's
# subtree without affecting IDA's strict warnings elsewhere.
if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang"
       AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    add_compile_options(
        -Wno-error=missing-template-arg-list-after-template-kw
        -Wno-missing-template-arg-list-after-template-kw)
endif()

add_subdirectory("${SFIZZ_PATH}" "${CMAKE_BINARY_DIR}/sfizz")
message(STATUS "sfizz configured from: ${SFIZZ_PATH}")

# -----------------------------------------------------------------------------
# Ableton Link — tempo / phase synchronization. Required transitively by
# otto-core's LinkSession.cpp. FetchContent pattern mirrors OTTO; same
# Feb-2026 pinned ref. Network is required at configure time the FIRST
# time only; the populated tree is cached under build/_deps/link-src.
# -----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    link
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG        addb7da)

set(LINK_BUILD_JAM      OFF CACHE BOOL "" FORCE)
set(LINK_BUILD_HUT      OFF CACHE BOOL "" FORCE)
set(LINK_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LINK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_GetProperties(link)
if(NOT link_POPULATED)
    FetchContent_Populate(link)
    include(${link_SOURCE_DIR}/AbletonLinkConfig.cmake)
endif()

message(STATUS "Ableton Link configured via FetchContent from: ${link_SOURCE_DIR}")

# -----------------------------------------------------------------------------
# otto-core — OTTO's shared C++ runtime (PlayerManager, GlobalMixer,
# TransportTracker — header-only; SfizzWrapper, LinkSession, EventBus,
# EnergyDefaults, FillBag, VoiceFamilyResolver, SongSnapshotBridge — .cpp).
# Gated TF Lite generation module is OFF in IDA: the generation pipeline is
# OTTO-only product surface, not IDA's. M-OTTO-1 wires the LINK; no IDA
# code references otto-core symbols yet — that lands in M-OTTO-2.
# -----------------------------------------------------------------------------
set(OTTO_CORE_PATH "${CMAKE_SOURCE_DIR}/external/OTTO/src/otto-core")

if(NOT EXISTS "${OTTO_CORE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "otto-core not found at ${OTTO_CORE_PATH}. "
        "Run: git submodule update --init --recursive")
endif()

set(OTTO_ENABLE_GENERATION OFF CACHE BOOL "Build OTTO's in-app content generation module (TF Lite, OTTO-only)" FORCE)

add_subdirectory("${OTTO_CORE_PATH}" "${CMAKE_BINARY_DIR}/otto-core")
message(STATUS "otto-core configured from: ${OTTO_CORE_PATH}")
