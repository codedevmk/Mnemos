include(FetchContent)

# zstd for save-state compression (ADR 0008), linked privately into the runtime.
if(NOT TARGET libzstd_static)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE INTERNAL "")
    set(ZSTD_BUILD_SHARED OFF CACHE INTERNAL "")
    set(ZSTD_BUILD_STATIC ON CACHE INTERNAL "")
    set(ZSTD_BUILD_TESTS OFF CACHE INTERNAL "")
    set(ZSTD_LEGACY_SUPPORT OFF CACHE INTERNAL "")
    FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        SOURCE_SUBDIR build/cmake
    )
    FetchContent_MakeAvailable(zstd)
endif()

add_library(
    mnemos_runtime
    scheduler.cpp
    input.cpp
    save_state.cpp
    rewind.cpp
)
add_library(mnemos::runtime ALIAS mnemos_runtime)

target_include_directories(
    mnemos_runtime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_runtime
    PUBLIC
        mnemos::foundation
        mnemos::security::cryptography
        mnemos::chips
        mnemos::topology
        mnemos::manifests
    PRIVATE
        libzstd_static
)

mnemos_apply_common_target_options(mnemos_runtime PUBLIC)
mnemos_declare_tier(
    NAME mnemos_runtime
    TIER 5
    DEPENDS
        mnemos_foundation
        mnemos_security_cryptography
        mnemos_chips
        mnemos_topology
        mnemos_manifests
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_runtime_test
        SOURCES
            tests/scheduler_test.cpp
            tests/input_test.cpp
            tests/save_state_test.cpp
            tests/rewind_test.cpp
        LIBRARIES mnemos::runtime
    )
endif()
