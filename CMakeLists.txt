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

# Third-party dependencies — vendored under external/ (see cmake/Dependencies.cmake).
include(cmake/Dependencies.cmake)

# Conceptual-time engine core — JUCE-free, deterministic, unit-testable.
add_subdirectory(core)

# Real-time engine layer — JUCE-free real-time primitives, retroactive ring, LMC.
add_subdirectory(engine)

# Session persistence — Constituent-graph JSON, content-addressed tape store.
add_subdirectory(persistence)

# Plugin host — VST3/AU scanning, generic parameter view (JUCE runtime).
add_subdirectory(host)

# Standalone application shell.
add_subdirectory(app)

if(SIRIUS_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_subdirectory(tests)
endif()
