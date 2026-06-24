add_library(
    mnemos_chips_video_tms9918a
    tms9918a.cpp
)
add_library(mnemos::chips::video::tms9918a ALIAS mnemos_chips_video_tms9918a)

target_include_directories(
    mnemos_chips_video_tms9918a
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_tms9918a PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_video_tms9918a PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_tms9918a TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_tms9918a_test
        SOURCES tests/tms9918a_test.cpp
        LIBRARIES mnemos::chips::video::tms9918a
    )
endif()
