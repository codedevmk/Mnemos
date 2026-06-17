add_library(
    mnemos_chips_audio_ym2413
    ym2413.cpp
)
add_library(mnemos::chips::audio::ym2413 ALIAS mnemos_chips_audio_ym2413)

target_include_directories(
    mnemos_chips_audio_ym2413
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_ym2413 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_ym2413 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_ym2413 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_ym2413_test
        SOURCES tests/ym2413_test.cpp
        LIBRARIES mnemos::chips::audio::ym2413
    )
endif()
