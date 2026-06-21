add_library(
    mnemos_chips_audio_mmc5
    mmc5.cpp
)
add_library(mnemos::chips::audio::mmc5 ALIAS mnemos_chips_audio_mmc5)

target_include_directories(
    mnemos_chips_audio_mmc5
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_mmc5 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_mmc5 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_mmc5 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_mmc5_test
        SOURCES
            tests/mmc5_test.cpp
        LIBRARIES mnemos::chips::audio::mmc5
    )
endif()
