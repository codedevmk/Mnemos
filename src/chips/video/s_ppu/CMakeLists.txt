add_library(
    mnemos_chips_video_s_ppu
    s_ppu.cpp
)
add_library(mnemos::chips::video::s_ppu ALIAS mnemos_chips_video_s_ppu)

target_include_directories(
    mnemos_chips_video_s_ppu
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_s_ppu PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_s_ppu PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_s_ppu TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_s_ppu_test
        SOURCES tests/s_ppu_test.cpp
        LIBRARIES mnemos::chips::video::s_ppu
    )
endif()
