include(FetchContent)

if(NOT TARGET tomlplusplus::tomlplusplus)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

add_library(
    mnemos_manifests_common
    src/manifest.cpp
)
add_library(mnemos::manifests::common ALIAS mnemos_manifests_common)

target_include_directories(
    mnemos_manifests_common
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

# toml++ is an implementation detail (not exposed in any public header), so it is
# linked PRIVATE and stays isolated to this tier-4 target (ADR 0007).
target_link_libraries(mnemos_manifests_common PRIVATE tomlplusplus::tomlplusplus)

mnemos_apply_common_target_options(mnemos_manifests_common PUBLIC)
mnemos_declare_tier(NAME mnemos_manifests_common TIER 4)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_common_test
        SOURCES tests/manifest_test.cpp
        LIBRARIES mnemos::manifests::common
    )
endif()
