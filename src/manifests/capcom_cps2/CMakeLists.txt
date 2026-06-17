# CPS2 (Capcom Play System 2) board data + helpers. Phase 1 delivered the CPS-2
# 68000 opcode cipher; phase 4 adds the board-assembler skeleton (cps2_system):
# load the encrypted program, build the decrypted opcode image, map the
# encrypted-data / decrypted-opcode split, wire work RAM, and boot the 68000.
# The full video / I/O / EEPROM map and QSound land in later increments.
add_library(
    mnemos_manifests_capcom_cps2
    cps2_crypto.cpp
    capcom_cps2_system.cpp
)
add_library(mnemos::manifests::capcom_cps2 ALIAS mnemos_manifests_capcom_cps2)

target_include_directories(
    mnemos_manifests_capcom_cps2
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The board assembler wires the 68000 on a topology bus out of the arcade ROM-set
# loader (manifests::common). The crypto is a pure transform with no extra deps.
target_link_libraries(
    mnemos_manifests_capcom_cps2
    PUBLIC
        mnemos::chips::cpu::m68000
        mnemos::topology
        mnemos::manifests::common
)

mnemos_apply_common_target_options(mnemos_manifests_capcom_cps2 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_capcom_cps2
    TIER 4
    DEPENDS
        mnemos_chips_cpu_m68000
        mnemos_topology
        mnemos_manifests_common
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_capcom_cps2_crypto_test
        SOURCES tests/cps2_crypto_test.cpp
        LIBRARIES mnemos::manifests::capcom_cps2
    )
    mnemos_add_test(
        mnemos_manifests_capcom_cps2_system_test
        SOURCES tests/capcom_cps2_system_test.cpp
        LIBRARIES mnemos::manifests::capcom_cps2
    )
endif()
