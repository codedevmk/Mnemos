add_library(
    mnemos_chips_video_vic_ii_6569
    src/vic_ii_6569.cpp
)
add_library(mnemos::chips::video::vic_ii_6569 ALIAS mnemos_chips_video_vic_ii_6569)

target_include_directories(
    mnemos_chips_video_vic_ii_6569
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_vic_ii_6569 PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_video_vic_ii_6569 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_vic_ii_6569 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_vic_ii_6569_test
        SOURCES tests/vic_ii_6569_test.cpp
        LIBRARIES mnemos::chips::video::vic_ii_6569
    )
endif()
