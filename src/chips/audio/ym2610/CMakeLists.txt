add_library(
    mnemos_chips_audio_ym2610
    ym2610.cpp
)
add_library(mnemos::chips::audio::ym2610 ALIAS mnemos_chips_audio_ym2610)

target_include_directories(
    mnemos_chips_audio_ym2610
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_chips_audio_ym2610
    PUBLIC
        mnemos::chips::shared
        mnemos::chips::audio::ssg
        mnemos::chips::audio::adpcm_a
        mnemos::chips::audio::adpcm_b
)

mnemos_apply_common_target_options(mnemos_chips_audio_ym2610 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_ym2610 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_ym2610_test
        SOURCES
            tests/ym2610_test.cpp
        LIBRARIES mnemos::chips::audio::ym2610
    )
endif()
