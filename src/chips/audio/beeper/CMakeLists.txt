add_library(
    mnemos_chips_audio_beeper
    beeper.cpp
)
add_library(mnemos::chips::audio::beeper ALIAS mnemos_chips_audio_beeper)

target_include_directories(
    mnemos_chips_audio_beeper
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_beeper PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_beeper PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_beeper TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_beeper_test
        SOURCES tests/beeper_test.cpp
        LIBRARIES mnemos::chips::audio::beeper
    )
endif()
