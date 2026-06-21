add_library(
    mnemos_chips_audio_rp2c33
    rp2c33.cpp
)
add_library(mnemos::chips::audio::rp2c33 ALIAS mnemos_chips_audio_rp2c33)

target_include_directories(
    mnemos_chips_audio_rp2c33
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_rp2c33 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_rp2c33 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_rp2c33 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_rp2c33_test
        SOURCES
            tests/rp2c33_test.cpp
        LIBRARIES mnemos::chips::audio::rp2c33
    )
endif()
