add_library(
    mnemos_chips_audio_dac8
    dac8.cpp
)
add_library(mnemos::chips::audio::dac8 ALIAS mnemos_chips_audio_dac8)

target_include_directories(
    mnemos_chips_audio_dac8
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_dac8 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_dac8 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_dac8 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_dac8_test
        SOURCES tests/dac8_test.cpp
        LIBRARIES mnemos::chips::audio::dac8
    )
endif()
