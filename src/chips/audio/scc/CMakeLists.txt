add_library(
    mnemos_chips_audio_scc
    scc.cpp
)
add_library(mnemos::chips::audio::scc ALIAS mnemos_chips_audio_scc)

target_include_directories(
    mnemos_chips_audio_scc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_scc PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_scc PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_scc TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_scc_test
        SOURCES
            tests/scc_test.cpp
        LIBRARIES mnemos::chips::audio::scc
    )
endif()
