add_library(
    mnemos_chips_video_cps_a_b
    cps_a_b.cpp
)
add_library(mnemos::chips::video::cps_a_b ALIAS mnemos_chips_video_cps_a_b)

target_include_directories(
    mnemos_chips_video_cps_a_b
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_cps_a_b PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_cps_a_b PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_cps_a_b TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_cps_a_b_test
        SOURCES tests/cps_a_b_test.cpp
        LIBRARIES mnemos::chips::video::cps_a_b
    )
endif()
