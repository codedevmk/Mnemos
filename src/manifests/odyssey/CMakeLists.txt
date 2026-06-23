add_library(
    mnemos_manifests_odyssey
    odyssey_system.cpp
)
add_library(mnemos::manifests::odyssey ALIAS mnemos_manifests_odyssey)

target_include_directories(
    mnemos_manifests_odyssey
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_odyssey
    PUBLIC
        mnemos::foundation
)

mnemos_apply_common_target_options(mnemos_manifests_odyssey PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_odyssey
    TIER 4
    DEPENDS
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_odyssey_test
        SOURCES tests/odyssey_system_test.cpp
        LIBRARIES mnemos::manifests::odyssey
    )
endif()
