add_library(
    mnemos_apps_player_genesis_adapter
    genesis_adapter.cpp
)
add_library(
    mnemos::apps::player::genesis_adapter
    ALIAS mnemos_apps_player_genesis_adapter
)

target_include_directories(
    mnemos_apps_player_genesis_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_genesis_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::genesis
        mnemos::runtime
)

mnemos_apply_common_target_options(mnemos_apps_player_genesis_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_genesis_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_genesis
        mnemos_runtime
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_genesis_adapter_test
        SOURCES tests/genesis_adapter_test.cpp
        LIBRARIES mnemos::apps::player::genesis_adapter
    )
endif()
