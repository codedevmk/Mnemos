add_library(
    mnemos_chips_video_sms_vdp
    src/sms_vdp.cpp
)
add_library(mnemos::chips::video::sms_vdp ALIAS mnemos_chips_video_sms_vdp)

target_include_directories(
    mnemos_chips_video_sms_vdp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_video_sms_vdp PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_video_sms_vdp PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_video_sms_vdp TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_video_sms_vdp_test
        SOURCES tests/sms_vdp_test.cpp
        LIBRARIES mnemos::chips::video::sms_vdp
    )
endif()
