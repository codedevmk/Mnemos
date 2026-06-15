add_library(
    mnemos_chips_video_agnus
    agnus.cpp
)
add_library(mnemos::chips::video::agnus ALIAS mnemos_chips_video_agnus)

target_include_directories(
    mnemos_chips_video_agnus
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_agnus PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_agnus PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_agnus TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_agnus_test
        SOURCES tests/agnus_test.cpp
        LIBRARIES mnemos::chips::video::agnus
    )
endif()
