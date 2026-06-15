add_library(
    mnemos_chips_video_denise
    denise.cpp
)
add_library(mnemos::chips::video::denise ALIAS mnemos_chips_video_denise)

target_include_directories(
    mnemos_chips_video_denise
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_denise PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_denise PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_denise TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_denise_test
        SOURCES tests/denise_test.cpp
        LIBRARIES mnemos::chips::video::denise
    )
endif()
