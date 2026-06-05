add_library(
    mnemos_chips_audio_rf5c68
    rf5c68.cpp
)
add_library(mnemos::chips::audio::rf5c68 ALIAS mnemos_chips_audio_rf5c68)

target_include_directories(
    mnemos_chips_audio_rf5c68
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_rf5c68 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_rf5c68 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_rf5c68 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_rf5c68_test
        SOURCES tests/rf5c68_test.cpp
        LIBRARIES mnemos::chips::audio::rf5c68
    )
endif()
