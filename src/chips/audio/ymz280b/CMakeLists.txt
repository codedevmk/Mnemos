add_library(
    mnemos_chips_audio_ymz280b
    ymz280b.cpp
)
add_library(mnemos::chips::audio::ymz280b ALIAS mnemos_chips_audio_ymz280b)

target_include_directories(
    mnemos_chips_audio_ymz280b
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_ymz280b PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_ymz280b PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_ymz280b TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_ymz280b_test
        SOURCES tests/ymz280b_test.cpp
        LIBRARIES mnemos::chips::audio::ymz280b
    )
endif()
