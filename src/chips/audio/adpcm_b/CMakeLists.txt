add_library(
    mnemos_chips_audio_adpcm_b
    adpcm_b.cpp
)
add_library(mnemos::chips::audio::adpcm_b ALIAS mnemos_chips_audio_adpcm_b)

target_include_directories(
    mnemos_chips_audio_adpcm_b
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_adpcm_b PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_adpcm_b PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_adpcm_b TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_adpcm_b_test
        SOURCES
            tests/adpcm_b_test.cpp
        LIBRARIES mnemos::chips::audio::adpcm_b
    )
endif()
