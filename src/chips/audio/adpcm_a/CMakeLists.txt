add_library(
    mnemos_chips_audio_adpcm_a
    adpcm_a.cpp
)
add_library(mnemos::chips::audio::adpcm_a ALIAS mnemos_chips_audio_adpcm_a)

target_include_directories(
    mnemos_chips_audio_adpcm_a
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_adpcm_a PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_adpcm_a PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_adpcm_a TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_adpcm_a_test
        SOURCES
            tests/adpcm_a_test.cpp
        LIBRARIES mnemos::chips::audio::adpcm_a
    )
endif()
