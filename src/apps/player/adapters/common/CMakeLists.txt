add_library(
    mnemos_apps_player_adapters_common
    cli_args.cpp
    region_args.cpp
    rom_loader.cpp
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
)

mnemos_apply_common_target_options(mnemos_apps_player_adapters_common PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_adapters_common
    TIER 7
    DEPENDS
        mnemos_chips_shared
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_adapters_common_test
        SOURCES
            tests/cli_args_test.cpp
            tests/region_args_test.cpp
            tests/rom_loader_test.cpp
        LIBRARIES mnemos::apps::player::adapters::common
    )
endif()
