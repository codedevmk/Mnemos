add_library(
    mnemos_chips_audio_vrc6
    vrc6.cpp
)
add_library(mnemos::chips::audio::vrc6 ALIAS mnemos_chips_audio_vrc6)

target_include_directories(
    mnemos_chips_audio_vrc6
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_vrc6 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_vrc6 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_vrc6 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_vrc6_test
        SOURCES
            tests/vrc6_test.cpp
        LIBRARIES mnemos::chips::audio::vrc6
    )
endif()
