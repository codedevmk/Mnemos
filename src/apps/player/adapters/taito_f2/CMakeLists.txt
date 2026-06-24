add_library(
    mnemos_apps_player_taito_f2_adapter
    taito_f2_adapter.cpp
)
add_library(mnemos::apps::player::taito_f2_adapter ALIAS mnemos_apps_player_taito_f2_adapter)

target_include_directories(
    mnemos_apps_player_taito_f2_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_taito_f2_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::taito_f2
        mnemos::peripheral::common
        mnemos::runtime
        mnemos::apps::player::adapters::common
)

mnemos_apply_common_target_options(mnemos_apps_player_taito_f2_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_taito_f2_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_taito_f2
        mnemos_peripheral_common
        mnemos_runtime
        mnemos_apps_player_adapters_common
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_taito_f2_adapter_test
        SOURCES tests/taito_f2_adapter_test.cpp
        LIBRARIES mnemos::apps::player::taito_f2_adapter mnemos::io
    )
endif()
