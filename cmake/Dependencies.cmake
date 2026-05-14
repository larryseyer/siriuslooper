# =============================================================================
# Sirius Looper — Third-Party Dependencies
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

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
message(STATUS "JUCE configured from: ${JUCE_PATH}")

# -----------------------------------------------------------------------------
# Catch2 — test framework (development only, not distributed)
# -----------------------------------------------------------------------------
if(SIRIUS_BUILD_TESTS)
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
# Statically linked: libsoxr is LGPL-2.1+, which is compatible with Sirius
# Looper's AGPLv3 software license, so static linking is permitted.
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
