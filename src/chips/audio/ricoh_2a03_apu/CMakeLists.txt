add_library(
    mnemos_chips_audio_ricoh_2a03_apu
    ricoh_2a03_apu.cpp
)
add_library(mnemos::chips::audio::ricoh_2a03_apu ALIAS mnemos_chips_audio_ricoh_2a03_apu)

target_include_directories(
    mnemos_chips_audio_ricoh_2a03_apu
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_ricoh_2a03_apu PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_ricoh_2a03_apu PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_ricoh_2a03_apu TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_ricoh_2a03_apu_test
        SOURCES
            tests/ricoh_2a03_apu_test.cpp
        LIBRARIES mnemos::chips::audio::ricoh_2a03_apu
    )
endif()
