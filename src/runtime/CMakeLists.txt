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
        GIT_TAG 794ea1b0afca0f020f4e57b6732332231fb23c70 # v1.5.6
        SOURCE_SUBDIR build/cmake
    )
    FetchContent_MakeAvailable(zstd)
endif()

add_library(
    mnemos_runtime
    scheduler.cpp
    input.cpp
    save_state.cpp
    machine_save.cpp
    session.cpp
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

    # Whole-machine save/load/rewind/input determinism, exercised end-to-end on a
    # real C64 built through the manifest path (tier 4, linked test-only -- a
    # downward dependency). No ROM is required or committed: the machine evolves
    # deterministically from reset with zero-filled system images.
    mnemos_add_test(
        mnemos_runtime_c64_save_state_test
        SOURCES
            tests/c64_save_state_roundtrip_test.cpp
            tests/session_c64_test.cpp
        LIBRARIES mnemos::runtime mnemos::manifests::c64
    )

    # Threaded-vs-synchronous determinism for the 32X dual-SH-2 scheduling
    # (tier-4 manifests::sega32x linked test-only -- a downward dependency).
    mnemos_add_test(
        mnemos_runtime_sega32x_determinism_test
        SOURCES tests/sega32x_determinism_test.cpp
        LIBRARIES mnemos::runtime mnemos::manifests::sega32x
    )
endif()
