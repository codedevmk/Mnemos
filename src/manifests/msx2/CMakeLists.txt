add_library(
    mnemos_manifests_msx2
    msx2_system.cpp
)
add_library(mnemos::manifests::msx2 ALIAS mnemos_manifests_msx2)

target_include_directories(
    mnemos_manifests_msx2
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_msx2
    PUBLIC
        mnemos::topology
        mnemos::manifests::common
        mnemos::chips::cpu::z80
        mnemos::chips::mapper::korean_msx_mapper
        mnemos::chips::video::v9938
        mnemos::chips::peripheral::msx_kanji_rom
        mnemos::chips::peripheral::rp5c01
        mnemos::chips::storage::msx_cassette
        mnemos::chips::storage::wd1793
        mnemos::chips::audio::scc
        mnemos::chips::audio::ssg
        mnemos::chips::audio::ym2413
)

mnemos_apply_common_target_options(mnemos_manifests_msx2 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_msx2
    TIER 4
    DEPENDS
        mnemos_manifests_common
        mnemos_topology
        mnemos_chips_shared
        mnemos_chips_cpu_z80
        mnemos_chips_mapper_korean_msx_mapper
        mnemos_chips_video_v9938
        mnemos_chips_peripheral_msx_kanji_rom
        mnemos_chips_peripheral_rp5c01
        mnemos_chips_storage_msx_cassette
        mnemos_chips_storage_wd1793
        mnemos_chips_audio_scc
        mnemos_chips_audio_ssg
        mnemos_chips_audio_ym2413
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_msx2_test
        SOURCES tests/msx2_system_test.cpp
        LIBRARIES mnemos::manifests::msx2
    )
endif()
