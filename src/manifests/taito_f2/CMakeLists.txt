file(GLOB _taito_f2_game_manifest_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/games/*.toml")
list(SORT _taito_f2_game_manifest_files)
set(MNEMOS_TAITO_F2_GAME_MANIFEST_COUNT 0)
set(MNEMOS_TAITO_F2_GAME_MANIFEST_ENTRIES "")
foreach(_f IN LISTS _taito_f2_game_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}"
    )
    get_filename_component(_set_id "${_f}" NAME_WE)
    file(READ "${_f}" _toml)
    string(
        APPEND
        MNEMOS_TAITO_F2_GAME_MANIFEST_ENTRIES
        "        {\"${_set_id}\", R\"MNEMOS_TOML(${_toml})MNEMOS_TOML\"},\n"
    )
    math(EXPR MNEMOS_TAITO_F2_GAME_MANIFEST_COUNT "${MNEMOS_TAITO_F2_GAME_MANIFEST_COUNT} + 1")
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/taito_f2_game_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/taito_f2_game_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_taito_f2
    taito_f2_system.cpp
)
add_library(mnemos::manifests::taito_f2 ALIAS mnemos_manifests_taito_f2)

target_include_directories(
    mnemos_manifests_taito_f2
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_taito_f2
    PUBLIC
        mnemos::chips::cpu::m68000
        mnemos::chips::cpu::z80
        mnemos::chips::bus_controller::tc0140syt
        mnemos::chips::video::taito_f2_video
        mnemos::chips::audio::ym2610
        mnemos::topology
        mnemos::manifests::common
        mnemos::security::cryptography
)

mnemos_apply_common_target_options(mnemos_manifests_taito_f2 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_taito_f2
    TIER 4
    DEPENDS
        mnemos_chips_cpu_m68000
        mnemos_chips_cpu_z80
        mnemos_chips_bus_controller_tc0140syt
        mnemos_chips_video_taito_f2_video
        mnemos_chips_audio_ym2610
        mnemos_topology
        mnemos_manifests_common
        mnemos_security_cryptography
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_taito_f2_system_test
        SOURCES tests/taito_f2_system_test.cpp
        LIBRARIES mnemos::manifests::taito_f2
    )
endif()
