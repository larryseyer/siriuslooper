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
