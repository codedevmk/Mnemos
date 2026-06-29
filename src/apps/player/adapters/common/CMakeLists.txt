add_library(
    mnemos_apps_player_adapters_common
    battery_save.cpp
    cli_args.cpp
    input_pack.cpp
    msx_keyboard_matrix.cpp
    msx_mouse_input.cpp
    region_args.cpp
    rom_loader.cpp
    state_file.cpp
    system_family.cpp
)
add_library(
    mnemos::apps::player::adapters::common
    ALIAS mnemos_apps_player_adapters_common
)

target_include_directories(
    mnemos_apps_player_adapters_common
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_adapters_common
    PUBLIC
        mnemos::chips::shared
        mnemos::peripheral::common
    PRIVATE
        mnemos::common
        mnemos::compression
        mnemos::io
)

mnemos_apply_common_target_options(mnemos_apps_player_adapters_common PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_adapters_common
    TIER 7
    DEPENDS
        mnemos_chips_shared
        mnemos_peripheral_common
        mnemos_common
        mnemos_compression
        mnemos_io
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_adapters_common_test
        SOURCES
            tests/battery_save_test.cpp
            tests/cli_args_test.cpp
            tests/input_pack_test.cpp
            tests/msx_keyboard_matrix_test.cpp
            tests/msx_mouse_input_test.cpp
            tests/region_args_test.cpp
            tests/rom_loader_test.cpp
            tests/state_file_test.cpp
            tests/system_family_test.cpp
        LIBRARIES mnemos::apps::player::adapters::common
    )
endif()
