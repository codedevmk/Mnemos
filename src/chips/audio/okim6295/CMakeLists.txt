add_library(
    mnemos_chips_audio_okim6295
    okim6295.cpp
)
add_library(mnemos::chips::audio::okim6295 ALIAS mnemos_chips_audio_okim6295)

target_include_directories(
    mnemos_chips_audio_okim6295
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_okim6295 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_okim6295 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_okim6295 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_okim6295_test
        SOURCES tests/okim6295_test.cpp
        LIBRARIES mnemos::chips::audio::okim6295
    )
endif()
