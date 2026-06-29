set(_irem_m119_game_manifest_files
    "games/scumimon.toml"
)
set(MNEMOS_IREM_M119_GAME_MANIFEST_COUNT 0)
set(MNEMOS_IREM_M119_EMBEDDED_CONSTANTS "")
set(MNEMOS_IREM_M119_EMBEDDED_ENTRIES "")
foreach(_f IN LISTS _irem_m119_game_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
    get_filename_component(_set_id "${_f}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_set_id}" _ident)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_f}" _toml)
    string(APPEND MNEMOS_IREM_M119_EMBEDDED_CONSTANTS
           "    inline constexpr std::string_view ${_ident}_toml = R\"MNEMOS_TOML(${_toml})MNEMOS_TOML\";\n\n")
    string(APPEND MNEMOS_IREM_M119_EMBEDDED_ENTRIES
           "        {\"${_set_id}\", ${_ident}_toml},\n")
    math(
        EXPR
        MNEMOS_IREM_M119_GAME_MANIFEST_COUNT
        "${MNEMOS_IREM_M119_GAME_MANIFEST_COUNT} + 1"
    )
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/m119_embedded_game_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/m119_embedded_game_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_irem_m119
    m119_game_manifests.cpp
    m119_system.cpp
)
add_library(mnemos::manifests::irem_m119 ALIAS mnemos_manifests_irem_m119)

target_include_directories(
    mnemos_manifests_irem_m119
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_irem_m119
    PUBLIC
        mnemos::chips::audio::ymz280b
        mnemos::chips::cpu::sh3
        mnemos::chips::video::upd94244
        mnemos::manifests::common
        mnemos::topology
    PRIVATE
        mnemos::security::cryptography
)

mnemos_apply_common_target_options(mnemos_manifests_irem_m119 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_irem_m119
    TIER 4
    DEPENDS
        mnemos_chips_audio_ymz280b
        mnemos_chips_cpu_sh3
        mnemos_chips_video_upd94244
        mnemos_manifests_common
        mnemos_topology
        mnemos_security_cryptography
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_irem_m119_test
        SOURCES tests/m119_rom_contract_test.cpp
        LIBRARIES
            mnemos::manifests::irem_m119
            mnemos::manifests::common
            mnemos::compression
            mnemos::io
    )
    target_compile_definitions(
        mnemos_manifests_irem_m119_test
        PRIVATE
            MNEMOS_IREM_M119_GAMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/games"
    )

    mnemos_add_test(
        mnemos_manifests_irem_m119_system_test
        SOURCES tests/m119_system_test.cpp
        LIBRARIES
            mnemos::manifests::irem_m119
            mnemos::runtime
    )
endif()
