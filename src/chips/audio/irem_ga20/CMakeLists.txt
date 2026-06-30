add_library(
    mnemos_chips_audio_irem_ga20
    irem_ga20.cpp
)
add_library(mnemos::chips::audio::irem_ga20 ALIAS mnemos_chips_audio_irem_ga20)

target_include_directories(
    mnemos_chips_audio_irem_ga20
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_irem_ga20 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_irem_ga20 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_irem_ga20 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_irem_ga20_test
        SOURCES tests/irem_ga20_test.cpp
        LIBRARIES mnemos::chips::audio::irem_ga20
    )
endif()
