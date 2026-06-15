add_library(
    mnemos_chips_video_ppu2c02
    ppu2c02.cpp
)
add_library(mnemos::chips::video::ppu2c02 ALIAS mnemos_chips_video_ppu2c02)

target_include_directories(
    mnemos_chips_video_ppu2c02
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_ppu2c02 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_ppu2c02 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_ppu2c02 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_ppu2c02_test
        SOURCES tests/ppu2c02_test.cpp
        LIBRARIES mnemos::chips::video::ppu2c02
    )
endif()
