# Embed the canonical Genesis manifests as compiled string constants (single
# source of truth; the player carries them with no filesystem dependency).
# Editing a source .toml re-runs configuration and regenerates the header.
set(_genesis_manifest_files "genesis.ntsc.toml" "genesis.pal.toml")
foreach(_f IN LISTS _genesis_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/genesis.ntsc.toml" MNEMOS_GENESIS_NTSC_TOML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/genesis.pal.toml" MNEMOS_GENESIS_PAL_TOML)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/genesis_embedded_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/genesis_embedded_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_genesis
    genesis_callbacks.cpp
    genesis_cart.cpp
    genesis_runtime.cpp
    genesis_system.cpp
    genesis_region.cpp
)
add_library(mnemos::manifests::genesis ALIAS mnemos_manifests_genesis)

target_include_directories(
    mnemos_manifests_genesis
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The Genesis assembler instantiates the concrete chips directly + plugs the
# default controller-port peripherals, so it links all of them.
target_link_libraries(
    mnemos_manifests_genesis
    PUBLIC
        mnemos::topology
        mnemos::manifests::common
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
        mnemos_manifests_common
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
        mnemos_manifests_genesis_cart_test
        SOURCES tests/genesis_cart_test.cpp
        LIBRARIES mnemos::manifests::genesis
    )
    mnemos_add_test(
        mnemos_manifests_genesis_region_test
        SOURCES tests/genesis_region_test.cpp
        LIBRARIES mnemos::manifests::genesis
    )
    # build_genesis_runtime vs assemble_genesis framebuffer parity. The synthetic
    # case always runs; the real-cart case is data-gated on MNEMOS_GENESIS_ROM
    # and SKIPs when unset (the synthetic case keeps the binary at exit 0).
    mnemos_add_test(
        mnemos_manifests_genesis_manifest_parity_test
        SOURCES tests/genesis_manifest_parity_test.cpp
        LIBRARIES mnemos::manifests::genesis mnemos::manifests mnemos::runtime
    )
    # Diagnostic WRAM-write tracer (data-gated; skips without MNEMOS_GENESIS_ROM).
    mnemos_add_test(
        mnemos_manifests_genesis_wram_watch_test
        SOURCES tests/genesis_wram_watch_test.cpp
        LIBRARIES mnemos::manifests::genesis mnemos::manifests mnemos::runtime
    )
    set_tests_properties(
        mnemos_manifests_genesis_wram_watch_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
