add_library(
    mnemos_manifests_nes
    fds.cpp
    nes_mapper.cpp
    nes_mapper_bandai.cpp
    nes_mapper_discrete.cpp
    nes_mapper_jaleco.cpp
    nes_mapper_konami.cpp
    nes_mapper_mmc1.cpp
    nes_mapper_mmc2_4.cpp
    nes_mapper_mmc3.cpp
    nes_mapper_mmc5.cpp
    nes_mapper_namco.cpp
    nes_mapper_sunsoft.cpp
    nes_mapper_taito.cpp
    nes_system.cpp
)
add_library(mnemos::manifests::nes ALIAS mnemos_manifests_nes)

target_include_directories(
    mnemos_manifests_nes
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The NES assembler instantiates the concrete 2A03 (m6510) + 2C02 PPU + APU and
# wires the topology bus, so it links them. The expansion-audio mappers own extra
# sound chips: the Sunsoft 5B (iNES 69) a YM2149 (ssg), the VRC6 (iNES 24/26) its
# pulse/sawtooth (vrc6), the VRC7 (iNES 85) an OPLL (ym2413), the Namco 163 (iNES
# 19) its wavetable (n163), the MMC5 (iNES 5) its 2 pulse + raw PCM (mmc5), and the
# FDS (rp2c33).
target_link_libraries(
    mnemos_manifests_nes
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::m6510
        mnemos::chips::video::ppu2c02
        mnemos::chips::audio::mmc5
        mnemos::chips::audio::n163
        mnemos::chips::audio::ricoh_2a03_apu
        mnemos::chips::audio::rp2c33
        mnemos::chips::audio::ssg
        mnemos::chips::audio::vrc6
        mnemos::chips::audio::ym2413
        mnemos::chips::storage::eeprom_i2c
)

mnemos_apply_common_target_options(mnemos_manifests_nes PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_nes
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_nes_test
        SOURCES
            tests/nes_system_test.cpp
            tests/nes_mapper_mmc2_4_test.cpp
        LIBRARIES mnemos::manifests::nes
    )
endif()
