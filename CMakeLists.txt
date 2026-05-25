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

option(MNEMOS_ENABLE_COVERAGE "Build Mnemos targets with LLVM source coverage instrumentation." OFF)

include(MnemosCompilerFlags)
include(MnemosDeclareTier)
include(CTest)

if(BUILD_TESTING)
    include(MnemosTesting)
endif()

add_subdirectory(src)
add_subdirectory(tools)
