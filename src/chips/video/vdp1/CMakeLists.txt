add_library(
    mnemos_chips_video_vdp1
    vdp1.cpp
)
add_library(mnemos::chips::video::vdp1 ALIAS mnemos_chips_video_vdp1)

target_include_directories(
    mnemos_chips_video_vdp1
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_vdp1 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_vdp1 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_vdp1 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_vdp1_test
        SOURCES tests/vdp1_test.cpp
        LIBRARIES mnemos::chips::video::vdp1
    )
endif()
