add_library(
    mnemos_manifests_taito_f2
    taito_f2_system.cpp
)
add_library(mnemos::manifests::taito_f2 ALIAS mnemos_manifests_taito_f2)

target_include_directories(
    mnemos_manifests_taito_f2
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_taito_f2
    PUBLIC
        mnemos::chips::cpu::m68000
        mnemos::chips::cpu::z80
        mnemos::chips::video::taito_f2_video
        mnemos::chips::audio::ym2610
        mnemos::topology
        mnemos::manifests::common
)

mnemos_apply_common_target_options(mnemos_manifests_taito_f2 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_taito_f2
    TIER 4
    DEPENDS
        mnemos_chips_cpu_m68000
        mnemos_chips_cpu_z80
        mnemos_chips_video_taito_f2_video
        mnemos_chips_audio_ym2610
        mnemos_topology
        mnemos_manifests_common
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_taito_f2_system_test
        SOURCES tests/taito_f2_system_test.cpp
        LIBRARIES mnemos::manifests::taito_f2
    )
endif()
