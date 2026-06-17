# CPS2 (Capcom Play System 2) board data + helpers. The manifest owns the CPS-2
# 68000 opcode cipher, encrypted-data / decrypted-opcode split, RAM / I/O /
# EEPROM map, QSound sound subsystem, and the shared CPS-A/CPS-B video chip.
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
        mnemos::chips::cpu::z80
        mnemos::chips::audio::qsound
        mnemos::chips::video::cps_a_b
        mnemos::chips::storage::eeprom_93c46
        mnemos::topology
        mnemos::manifests::common
)

mnemos_apply_common_target_options(mnemos_manifests_capcom_cps2 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_capcom_cps2
    TIER 4
    DEPENDS
        mnemos_chips_cpu_m68000
        mnemos_chips_cpu_z80
        mnemos_chips_audio_qsound
        mnemos_chips_video_cps_a_b
        mnemos_chips_storage_eeprom_93c46
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
