add_library(
    mnemos_chips_audio_ym2612
    ym2612.cpp
)
add_library(mnemos::chips::audio::ym2612 ALIAS mnemos_chips_audio_ym2612)

target_include_directories(
    mnemos_chips_audio_ym2612
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_ym2612 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_ym2612 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_ym2612 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_ym2612_test
        SOURCES tests/ym2612_test.cpp
        LIBRARIES mnemos::chips::audio::ym2612
    )
endif()
