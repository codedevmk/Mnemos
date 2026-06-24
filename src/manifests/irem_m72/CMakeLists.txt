set(_irem_m72_game_manifest_files
    "games/airdueljm72.toml"
    "games/airduelm72.toml"
    "games/bchopper.toml"
    "games/dbreedjm72.toml"
    "games/dbreedm72.toml"
    "games/dkgensanm72.toml"
    "games/gallopm72.toml"
    "games/imgfight.toml"
    "games/imgfightj.toml"
    "games/imgfightjb.toml"
    "games/loht.toml"
    "games/lohtb2.toml"
    "games/lohtb3.toml"
    "games/lohtj.toml"
    "games/mrheli.toml"
    "games/nspirit.toml"
    "games/nspiritj.toml"
    "games/rtype.toml"
    "games/rtypeb.toml"
    "games/rtypej.toml"
    "games/rtypejp.toml"
    "games/rtypeu.toml"
    "games/xmultiplm72.toml"
)
set(MNEMOS_IREM_M72_GAME_MANIFEST_COUNT 0)
set(MNEMOS_IREM_M72_EMBEDDED_CONSTANTS "")
set(MNEMOS_IREM_M72_EMBEDDED_ENTRIES "")
foreach(_f IN LISTS _irem_m72_game_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
    get_filename_component(_set_id "${_f}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_set_id}" _ident)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_f}" _toml)
    string(APPEND MNEMOS_IREM_M72_EMBEDDED_CONSTANTS
           "    inline constexpr std::string_view ${_ident}_toml = R\"MNEMOS_TOML(${_toml})MNEMOS_TOML\";\n\n")
    string(APPEND MNEMOS_IREM_M72_EMBEDDED_ENTRIES
           "        {\"${_set_id}\", ${_ident}_toml},\n")
    math(
        EXPR
        MNEMOS_IREM_M72_GAME_MANIFEST_COUNT
        "${MNEMOS_IREM_M72_GAME_MANIFEST_COUNT} + 1"
    )
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/m72_embedded_game_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/m72_embedded_game_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_irem_m72
    m72_system.cpp
)
add_library(mnemos::manifests::irem_m72 ALIAS mnemos_manifests_irem_m72)

target_include_directories(
    mnemos_manifests_irem_m72
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The M72 board: V30 + Z80 on their own buses, ROM regions out of the arcade
# ROM-set loader (manifests::common).
target_link_libraries(
    mnemos_manifests_irem_m72
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::v30
        mnemos::chips::cpu::z80
        mnemos::chips::cpu::mcs51
        mnemos::chips::bus_controller::pic_8259
        mnemos::chips::video::irem_m72_video
        mnemos::chips::audio::ym2151
        mnemos::chips::audio::dac8
        mnemos::manifests::common
        mnemos::security::cryptography
)

mnemos_apply_common_target_options(mnemos_manifests_irem_m72 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_irem_m72
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
        mnemos_manifests_irem_m72_test
        SOURCES tests/m72_system_test.cpp
        LIBRARIES mnemos::manifests::irem_m72 mnemos::runtime
    )
    target_compile_definitions(
        mnemos_manifests_irem_m72_test
        PRIVATE
            MNEMOS_IREM_M72_GAMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/games"
    )
endif()
