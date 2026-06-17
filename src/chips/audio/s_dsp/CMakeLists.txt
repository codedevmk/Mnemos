add_library(
    mnemos_chips_audio_s_dsp
    s_dsp.cpp
)
add_library(mnemos::chips::audio::s_dsp ALIAS mnemos_chips_audio_s_dsp)

target_include_directories(
    mnemos_chips_audio_s_dsp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_s_dsp PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_s_dsp PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_s_dsp TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_s_dsp_test
        SOURCES
            tests/s_dsp_test.cpp
        LIBRARIES mnemos::chips::audio::s_dsp
    )
endif()
