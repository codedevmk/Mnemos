add_library(
    mnemos_chips_video_ula
    ula.cpp
)
add_library(mnemos::chips::video::ula ALIAS mnemos_chips_video_ula)

target_include_directories(
    mnemos_chips_video_ula
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_ula PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_ula PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_ula TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_ula_test
        SOURCES tests/ula_test.cpp
        LIBRARIES mnemos::chips::video::ula
    )
endif()
