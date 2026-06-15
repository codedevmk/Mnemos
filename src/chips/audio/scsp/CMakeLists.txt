add_library(
    mnemos_chips_audio_scsp
    scsp.cpp
)
add_library(mnemos::chips::audio::scsp ALIAS mnemos_chips_audio_scsp)

target_include_directories(
    mnemos_chips_audio_scsp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_scsp PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_scsp PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_scsp TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_scsp_test
        SOURCES
            tests/scsp_test.cpp
        LIBRARIES mnemos::chips::audio::scsp
    )
endif()
