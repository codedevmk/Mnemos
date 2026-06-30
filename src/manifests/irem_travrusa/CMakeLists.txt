set(_irem_travrusa_game_manifest_files
    "games/travrusa.toml"
    "games/motorace.toml"
    "games/travrusab.toml"
    "games/travrusab2.toml"
)
set(MNEMOS_IREM_TRAVRUSA_GAME_MANIFEST_COUNT 0)
set(MNEMOS_IREM_TRAVRUSA_EMBEDDED_CONSTANTS "")
set(MNEMOS_IREM_TRAVRUSA_EMBEDDED_ENTRIES "")
foreach(_f IN LISTS _irem_travrusa_game_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
    get_filename_component(_set_id "${_f}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${_set_id}" _ident)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_f}" _toml)
    string(APPEND MNEMOS_IREM_TRAVRUSA_EMBEDDED_CONSTANTS
           "    inline constexpr std::string_view ${_ident}_toml = R\"MNEMOS_TOML(${_toml})MNEMOS_TOML\";\n\n")
    string(APPEND MNEMOS_IREM_TRAVRUSA_EMBEDDED_ENTRIES
           "        {\"${_set_id}\", ${_ident}_toml},\n")
    math(
        EXPR
        MNEMOS_IREM_TRAVRUSA_GAME_MANIFEST_COUNT
        "${MNEMOS_IREM_TRAVRUSA_GAME_MANIFEST_COUNT} + 1"
    )
endforeach()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/travrusa_embedded_game_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/travrusa_embedded_game_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_irem_travrusa
    travrusa_system.cpp
)
add_library(mnemos::manifests::irem_travrusa ALIAS mnemos_manifests_irem_travrusa)

target_include_directories(
    mnemos_manifests_irem_travrusa
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_irem_travrusa
    PUBLIC
        mnemos::chips::audio::msm5205
        mnemos::chips::audio::ssg
        mnemos::chips::cpu::z80
        mnemos::manifests::common
        mnemos::security::cryptography
        mnemos::topology
)

mnemos_apply_common_target_options(mnemos_manifests_irem_travrusa PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_irem_travrusa
    TIER 4
    DEPENDS
        mnemos_chips_audio_msm5205
        mnemos_chips_audio_ssg
        mnemos_chips_cpu_z80
        mnemos_chips_shared
        mnemos_manifests_common
        mnemos_security_cryptography
        mnemos_topology
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_irem_travrusa_test
        SOURCES tests/travrusa_rom_contract_test.cpp
        LIBRARIES
            mnemos::manifests::irem_travrusa
            mnemos::manifests::common
            mnemos::compression
            mnemos::io
    )
    target_compile_definitions(
        mnemos_manifests_irem_travrusa_test
        PRIVATE
            MNEMOS_IREM_TRAVRUSA_GAMES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/games"
    )
    mnemos_add_test(
        mnemos_manifests_irem_travrusa_system_test
        SOURCES tests/travrusa_system_test.cpp
        LIBRARIES
            mnemos::manifests::irem_travrusa
            mnemos::manifests::common
    )
endif()
