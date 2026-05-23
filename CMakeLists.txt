cmake_minimum_required(VERSION 3.22)

project(SiriusLooper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SIRIUS_BUILD_TESTS "Build the Sirius Looper test suite" ON)

# OTTO asset bundle (IRs, samples, fonts, GUI assets, models). Gitignored
# under external/OTTO/assets/ because of copyright; at dev time Sirius reads
# from the operator's OTTO checkout, at install time the bundling pipeline
# copies the bundle into the install. Injected into SiriusEngine as a
# PRIVATE compile definition (SIRIUS_OTTO_ASSETS_DIR) — only RvbAdapter.cpp
# currently consumes it (T3d default IR resolution).
set(OTTO_ASSETS_DIR "/Users/larryseyer/AudioDevelopment/OTTO/assets"
    CACHE PATH "Path to OTTO's asset bundle (IRs, samples, fonts) at dev time.")
if(NOT EXISTS "${OTTO_ASSETS_DIR}")
    message(STATUS
        "OTTO_ASSETS_DIR='${OTTO_ASSETS_DIR}' does not exist; "
        "internal-FX features that need OTTO assets (e.g. RVB IR loading) "
        "will degrade gracefully at runtime.")
endif()

# Third-party dependencies — vendored under external/ (see cmake/Dependencies.cmake).
include(cmake/Dependencies.cmake)

# Conceptual-time engine core — JUCE-free, deterministic, unit-testable.
add_subdirectory(core)

# Real-time engine layer — JUCE-free real-time primitives, retroactive ring, LMC.
add_subdirectory(engine)

# JUCE audio-device bridge — the thin layer the engine comment alludes to,
# turning juce::AudioIODeviceCallback into the single audio-thread entry point.
add_subdirectory(audio)

# Session persistence — Constituent-graph JSON, content-addressed tape store.
add_subdirectory(persistence)

# Plugin host — VST3/AU scanning, generic parameter view (JUCE runtime).
add_subdirectory(host)

# The performer's instrument — undo, latency budget, performance/preparation views.
add_subdirectory(ui)

# Ensemble — LMC election, CRDT session merge, transport messages.
add_subdirectory(net)

# Video — VideoFrame data model, frame-rate-conversion membrane, preview surface.
add_subdirectory(video)

# Standalone application shell.
add_subdirectory(app)

# Out-of-process plug-in host binary — spawned once per hosted plug-in
# instance (V7 §9.1, plan M7). Separate process intentionally: a plug-in
# crash must not take the always-running tape with it.
add_subdirectory(host_process)

if(SIRIUS_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_subdirectory(tests)
endif()
