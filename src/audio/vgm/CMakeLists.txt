add_library(
    mnemos_audio_vgm
    vgm.cpp
)
add_library(mnemos::audio::vgm ALIAS mnemos_audio_vgm)

target_include_directories(
    mnemos_audio_vgm
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_audio_vgm PUBLIC mnemos::io)

mnemos_apply_common_target_options(mnemos_audio_vgm PUBLIC)
mnemos_declare_tier(
    NAME mnemos_audio_vgm
    TIER 2
    DEPENDS mnemos_io
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_audio_vgm_test
        SOURCES tests/vgm_test.cpp
        LIBRARIES mnemos::audio::vgm
    )
endif()
