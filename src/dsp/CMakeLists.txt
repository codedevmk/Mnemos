add_library(mnemos_dsp INTERFACE)
add_library(mnemos::dsp ALIAS mnemos_dsp)

target_include_directories(
    mnemos_dsp
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_dsp INTERFACE)
mnemos_declare_tier(NAME mnemos_dsp TIER 1)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_dsp_test
        SOURCES tests/audio_resampler_test.cpp
        LIBRARIES mnemos::dsp
    )
endif()
