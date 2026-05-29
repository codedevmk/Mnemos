# Embed the canonical C64 manifests as compiled string constants (single source
# of truth; editing a source .toml re-runs configuration and regenerates).
set(_c64_manifest_files "c64.ntsc.toml" "c64.pal.toml")
foreach(_f IN LISTS _c64_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/c64.ntsc.toml" MNEMOS_C64_NTSC_TOML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/c64.pal.toml" MNEMOS_C64_PAL_TOML)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/c64_embedded_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/c64_embedded_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_c64
    c64_callbacks.cpp
    c64_system.cpp
    c64_input.cpp
)
add_library(mnemos::manifests::c64 ALIAS mnemos_manifests_c64)

target_include_directories(
    mnemos_manifests_c64
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The C64 assembler instantiates the concrete C64 chips directly, so it links them
# (no factory-id lookup, no static-init stripping concern).
target_link_libraries(
    mnemos_manifests_c64
    PUBLIC
        mnemos::topology
        mnemos::manifests::common
        mnemos::chips::cpu::m6510
        mnemos::chips::video::vic_ii_6569
        mnemos::chips::audio::sid_6581
        mnemos::chips::bus_controller::cia_6526
        mnemos::chips::mapper::c64_pla
        mnemos::chips::mapper::c64_cartridge
        mnemos::chips::peripheral::modem
        mnemos::chips::peripheral::reu
        mnemos::chips::peripheral::rs232
        mnemos::chips::storage::c1541
        mnemos::chips::storage::datasette
)

mnemos_apply_common_target_options(mnemos_manifests_c64 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_c64
    TIER 4
    DEPENDS mnemos_topology mnemos_manifests_common mnemos_chips_shared mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_c64_test
        SOURCES tests/c64_system_test.cpp
        LIBRARIES mnemos::manifests::c64
    )
    # PLA dynamic-banking through the manifest path (build_system + c64.bank.*
    # overlay predicates). Synthetic ROMs, always-on.
    mnemos_add_test(
        mnemos_manifests_c64_banking_test
        SOURCES tests/c64_manifest_banking_test.cpp
        LIBRARIES mnemos::manifests::c64 mnemos::manifests
    )
endif()
