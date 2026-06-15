# CPS1 (Capcom Play System 1) board data + board assembler. Phase 3 delivered
# the hardware-keyed CPS-B profile / gfx-mapper census; increment 1 of the board
# manifest added the cps1_system assembler (68000 main CPU + cps_a_b video),
# increment 2 the per-frame video decode + vblank IRQ, and increment 3 the Z80
# sound subsystem (sound Z80 + YM2151 + OKIM6295 on the sound bus). IO/DIP
# inputs, sprite-latch DMA, CPS-B protection read-back, and the player adapter
# land in later increments.
add_library(
    mnemos_manifests_capcom_cps1
    cps_b_profiles.cpp
    capcom_cps1_system.cpp
)
add_library(mnemos::manifests::capcom_cps1 ALIAS mnemos_manifests_capcom_cps1)

target_include_directories(
    mnemos_manifests_capcom_cps1
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The profile census is expressed in the cps_a_b chip's profile/mapper types;
# the board assembler wires the 68000 + cps_a_b on a topology bus out of the
# arcade ROM-set loader (manifests::common).
target_link_libraries(
    mnemos_manifests_capcom_cps1
    PUBLIC
        mnemos::chips::cpu::m68000
        mnemos::chips::cpu::z80
        mnemos::chips::video::cps_a_b
        mnemos::chips::audio::ym2151
        mnemos::chips::audio::okim6295
        mnemos::topology
        mnemos::manifests::common
)

mnemos_apply_common_target_options(mnemos_manifests_capcom_cps1 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_capcom_cps1
    TIER 4
    DEPENDS
        mnemos_chips_cpu_m68000
        mnemos_chips_cpu_z80
        mnemos_chips_video_cps_a_b
        mnemos_chips_audio_ym2151
        mnemos_chips_audio_okim6295
        mnemos_topology
        mnemos_manifests_common
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_capcom_cps1_profile_test
        SOURCES tests/cps_b_profile_test.cpp
        LIBRARIES mnemos::manifests::capcom_cps1
    )
    mnemos_add_test(
        mnemos_manifests_capcom_cps1_system_test
        SOURCES tests/capcom_cps1_system_test.cpp
        LIBRARIES mnemos::manifests::capcom_cps1
    )
endif()
