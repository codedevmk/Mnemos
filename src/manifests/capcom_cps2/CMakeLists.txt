# CPS2 (Capcom Play System 2) board data + helpers. The manifest owns the CPS-2
# 68000 opcode cipher, encrypted-data / decrypted-opcode split, RAM / I/O /
# EEPROM map, QSound sound subsystem, the CPS-2 video chip, and the checked-in
# game ROM-set declarations. The declarations are embedded so the player can
# open authentic set zips without requiring users to inject a game.toml copy.
file(GLOB _cps2_game_manifest_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/games/*.toml")
list(SORT _cps2_game_manifest_files)
set(MNEMOS_CPS2_GAME_MANIFEST_COUNT 0)
set(MNEMOS_CPS2_GAME_MANIFEST_ENTRIES "")
foreach(_f IN LISTS _cps2_game_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}"
    )
    get_filename_component(_set_id "${_f}" NAME_WE)
    file(READ "${_f}" _toml)
    string(
        APPEND
        MNEMOS_CPS2_GAME_MANIFEST_ENTRIES
        "        {\"${_set_id}\", R\"MNEMOS_TOML(${_toml})MNEMOS_TOML\"},\n"
    )
    math(EXPR MNEMOS_CPS2_GAME_MANIFEST_COUNT "${MNEMOS_CPS2_GAME_MANIFEST_COUNT} + 1")
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cps2_game_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cps2_game_manifests.hpp"
    @ONLY
)

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
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
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
        mnemos::chips::video::cps2_video
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
        mnemos_chips_video_cps2_video
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
    target_compile_definitions(
        mnemos_manifests_capcom_cps2_system_test
        PRIVATE MNEMOS_CPS2_GAMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/games"
    )
endif()
