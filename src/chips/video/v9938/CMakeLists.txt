add_library(
    mnemos_chips_video_v9938
    v9938.cpp
)
add_library(mnemos::chips::video::v9938 ALIAS mnemos_chips_video_v9938)

target_include_directories(
    mnemos_chips_video_v9938
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_v9938 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_v9938 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_v9938 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_v9938_test
        SOURCES tests/v9938_test.cpp
        LIBRARIES mnemos::chips::video::v9938
    )
endif()
