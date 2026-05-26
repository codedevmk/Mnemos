add_library(
    mnemos_apps_player_adapters_common
    region.cpp
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

mnemos_apply_common_target_options(mnemos_apps_player_adapters_common PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_adapters_common
    TIER 8
    DEPENDS mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_adapters_common_test
        SOURCES tests/region_test.cpp
        LIBRARIES mnemos::apps::player::adapters::common
    )
endif()
