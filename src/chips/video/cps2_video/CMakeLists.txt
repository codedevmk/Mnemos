add_library(
    mnemos_chips_video_cps2_video
    cps2_video.cpp
)
add_library(mnemos::chips::video::cps2_video ALIAS mnemos_chips_video_cps2_video)

target_include_directories(
    mnemos_chips_video_cps2_video
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_cps2_video PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_cps2_video PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_cps2_video TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_cps2_video_test
        SOURCES tests/cps2_video_test.cpp
        LIBRARIES mnemos::chips::video::cps2_video
    )
endif()
