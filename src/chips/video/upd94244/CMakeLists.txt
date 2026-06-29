add_library(
    mnemos_chips_video_upd94244
    upd94244.cpp
)
add_library(mnemos::chips::video::upd94244 ALIAS mnemos_chips_video_upd94244)

target_include_directories(
    mnemos_chips_video_upd94244
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_upd94244 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_upd94244 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_upd94244 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_upd94244_test
        SOURCES tests/upd94244_test.cpp
        LIBRARIES mnemos::chips::video::upd94244
    )
endif()
