add_library(
    mnemos_manifests_genesis
    genesis_system.cpp
    genesis_region.cpp
)
add_library(mnemos::manifests::genesis ALIAS mnemos_manifests_genesis)

target_include_directories(
    mnemos_manifests_genesis
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The Genesis assembler instantiates the concrete chips directly + plugs the
# default controller-port peripherals, so it links all of them.
target_link_libraries(
    mnemos_manifests_genesis
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::m68000
        mnemos::chips::cpu::z80
        mnemos::chips::video::genesis_vdp
        mnemos::chips::audio::ym2612
        mnemos::chips::audio::sn76489
        mnemos::peripheral::input
)

mnemos_apply_common_target_options(mnemos_manifests_genesis PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_genesis
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_chips_shared
        mnemos_foundation
        mnemos_peripheral_common
        mnemos_peripheral_input
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_genesis_test
        SOURCES tests/genesis_system_test.cpp
        LIBRARIES mnemos::manifests::genesis
    )
    mnemos_add_test(
        mnemos_manifests_genesis_region_test
        SOURCES tests/genesis_region_test.cpp
        LIBRARIES mnemos::manifests::genesis
    )
endif()
