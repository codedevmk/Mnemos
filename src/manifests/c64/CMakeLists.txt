add_library(
    mnemos_manifests_c64
    src/c64_system.cpp
)
add_library(mnemos::manifests::c64 ALIAS mnemos_manifests_c64)

target_include_directories(
    mnemos_manifests_c64
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

# The C64 assembler instantiates the concrete C64 chips directly, so it links them
# (no factory-id lookup, no static-init stripping concern).
target_link_libraries(
    mnemos_manifests_c64
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::m6510
        mnemos::chips::video::vic_ii_6569
        mnemos::chips::audio::sid_6581
        mnemos::chips::bus_controller::cia_6526
        mnemos::chips::mapper::c64_pla
        mnemos::chips::storage::c1541
)

mnemos_apply_common_target_options(mnemos_manifests_c64 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_c64
    TIER 4
    DEPENDS mnemos_topology mnemos_chips_common mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_c64_test
        SOURCES tests/c64_system_test.cpp
        LIBRARIES mnemos::manifests::c64
    )
endif()
