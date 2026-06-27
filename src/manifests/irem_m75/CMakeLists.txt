set(_irem_m75_game_manifest_files
    "games/vigilant.toml"
    "games/vigilanta.toml"
    "games/vigilantb.toml"
    "games/vigilantbl.toml"
    "games/vigilantc.toml"
    "games/vigilantd.toml"
    "games/vigilantg.toml"
    "games/vigilanto.toml"
)
set(MNEMOS_IREM_M75_GAME_MANIFEST_COUNT 0)
set(MNEMOS_IREM_M75_EMBEDDED_CONSTANTS "")
set(MNEMOS_IREM_M75_EMBEDDED_ENTRIES "")
foreach(_f IN LISTS _irem_m75_game_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
    get_filename_component(_set_id "${_f}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_set_id}" _ident)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_f}" _toml)
    string(APPEND MNEMOS_IREM_M75_EMBEDDED_CONSTANTS
           "    inline constexpr std::string_view ${_ident}_toml = R\"MNEMOS_TOML(${_toml})MNEMOS_TOML\";\n\n")
    string(APPEND MNEMOS_IREM_M75_EMBEDDED_ENTRIES
           "        {\"${_set_id}\", ${_ident}_toml},\n")
    math(
        EXPR
        MNEMOS_IREM_M75_GAME_MANIFEST_COUNT
        "${MNEMOS_IREM_M75_GAME_MANIFEST_COUNT} + 1"
    )
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/m75_embedded_game_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/m75_embedded_game_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_irem_m75
    m75_system.cpp
)
add_library(mnemos::manifests::irem_m75 ALIAS mnemos_manifests_irem_m75)

target_include_directories(
    mnemos_manifests_irem_m75
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_irem_m75
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::z80
        mnemos::chips::audio::ym2151
        mnemos::chips::audio::dac8
        mnemos::manifests::common
        mnemos::security::cryptography
)

mnemos_apply_common_target_options(mnemos_manifests_irem_m75 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_irem_m75
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_manifests_common
        mnemos_security_cryptography
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_irem_m75_test
        SOURCES tests/m75_rom_contract_test.cpp
        LIBRARIES
            mnemos::manifests::irem_m75
            mnemos::manifests::common
            mnemos::compression
            mnemos::io
    )
    target_compile_definitions(
        mnemos_manifests_irem_m75_test
        PRIVATE
            MNEMOS_IREM_M75_GAMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/games"
    )
    mnemos_add_test(
        mnemos_manifests_irem_m75_system_test
        SOURCES tests/m75_system_test.cpp
        LIBRARIES
            mnemos::manifests::irem_m75
            mnemos::manifests::common
    )
endif()
