add_library(
    mnemos_chips_audio_sid_6581
    sid_6581.cpp
)
add_library(mnemos::chips::audio::sid_6581 ALIAS mnemos_chips_audio_sid_6581)

target_include_directories(
    mnemos_chips_audio_sid_6581
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_sid_6581 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_sid_6581 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_sid_6581 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_sid_6581_test
        SOURCES tests/sid_6581_test.cpp
        LIBRARIES mnemos::chips::audio::sid_6581
    )
endif()
