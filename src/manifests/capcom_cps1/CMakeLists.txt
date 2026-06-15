# CPS1 (Capcom Play System 1) board data + (later) system assembler. Phase 3
# delivers the hardware-keyed CPS-B profile / gfx-mapper census; the board
# manifest, adapter, and ROM conformance land in later increments.
add_library(
    mnemos_manifests_capcom_cps1
    cps_b_profiles.cpp
)
add_library(mnemos::manifests::capcom_cps1 ALIAS mnemos_manifests_capcom_cps1)

target_include_directories(
    mnemos_manifests_capcom_cps1
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The profile census is expressed in the cps_a_b chip's profile/mapper types.
target_link_libraries(
    mnemos_manifests_capcom_cps1
    PUBLIC
        mnemos::chips::video::cps_a_b
)

mnemos_apply_common_target_options(mnemos_manifests_capcom_cps1 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_capcom_cps1
    TIER 4
    DEPENDS
        mnemos_chips_video_cps_a_b
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_capcom_cps1_profile_test
        SOURCES tests/cps_b_profile_test.cpp
        LIBRARIES mnemos::manifests::capcom_cps1
    )
endif()
