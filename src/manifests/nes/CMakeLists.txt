add_library(
    mnemos_manifests_nes
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
# wires the topology bus, so it links them.
target_link_libraries(
    mnemos_manifests_nes
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::m6510
        mnemos::chips::video::ppu2c02
        mnemos::chips::audio::ricoh_2a03_apu
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
        SOURCES tests/nes_system_test.cpp
        LIBRARIES mnemos::manifests::nes
    )
endif()
