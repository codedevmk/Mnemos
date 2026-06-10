cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use CMakePresets.json and build/<preset>.")
endif()

project(
    Mnemos
    VERSION 0.1.0
    DESCRIPTION "Deterministic multi-system emulator framework and developer toolkit"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release: whole-program optimization. The emulation hot path crosses
# translation units (CPU cores call the bus through an interface); LTO lets
# the linker inline and devirtualize across them. Measured on the 32X
# in-game run before enabling by default.
option(MNEMOS_ENABLE_LTO "Whole-program optimization for Release builds." OFF)
if(MNEMOS_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

option(MNEMOS_ENABLE_COVERAGE "Build Mnemos targets with LLVM source coverage instrumentation." OFF)
option(MNEMOS_BUILD_APPS "Build the SDL3-backed frontend applications (src/apps). Disable for headless core + test builds that avoid the SDL3/windowing dependency." ON)

include(MnemosCompilerFlags)
include(MnemosDeclareTier)
include(CTest)

if(BUILD_TESTING)
    include(MnemosTesting)
endif()

add_subdirectory(src)
add_subdirectory(tools)

# APM tracer sidecar: a self-contained capability (NOT part of the engine in
# src/). It loads the engine as a runtime plugin and observes it from the
# outside. Added after src/ so it can bind against the engine's public targets.
add_subdirectory(apm)

# Cross-system tests (golden boot, integration). Added after tools/ so the shared
# CLI core target they reuse already exists.
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
