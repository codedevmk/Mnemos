add_library(
    mnemos_chips_audio_sn76489
    src/sn76489.cpp
)
add_library(mnemos::chips::audio::sn76489 ALIAS mnemos_chips_audio_sn76489)

target_include_directories(
    mnemos_chips_audio_sn76489
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_sn76489 PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_audio_sn76489 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_sn76489 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_sn76489_test
        SOURCES tests/sn76489_test.cpp
        LIBRARIES mnemos::chips::audio::sn76489
    )
endif()
