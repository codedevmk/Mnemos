add_library(
    mnemos_apps_player_taito_gnet_adapter
    taito_gnet_adapter.cpp
)
add_library(mnemos::apps::player::taito_gnet_adapter ALIAS mnemos_apps_player_taito_gnet_adapter)

target_include_directories(
    mnemos_apps_player_taito_gnet_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_taito_gnet_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::taito_gnet
        mnemos::runtime
    PRIVATE
        mnemos::security::cryptography
)

mnemos_apply_common_target_options(mnemos_apps_player_taito_gnet_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_taito_gnet_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_taito_gnet
        mnemos_runtime
        mnemos_security_cryptography
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_taito_gnet_adapter_test
        SOURCES tests/taito_gnet_adapter_test.cpp
        LIBRARIES mnemos::apps::player::taito_gnet_adapter
    )
endif()
