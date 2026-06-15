add_library(
    mnemos_chips_audio_qsound
    qsound.cpp
)
add_library(mnemos::chips::audio::qsound ALIAS mnemos_chips_audio_qsound)

target_include_directories(
    mnemos_chips_audio_qsound
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_qsound PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_qsound PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_qsound TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_qsound_test
        SOURCES tests/qsound_test.cpp
        LIBRARIES mnemos::chips::audio::qsound
    )
endif()
