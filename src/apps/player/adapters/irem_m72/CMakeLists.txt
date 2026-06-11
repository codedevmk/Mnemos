add_library(
    mnemos_apps_player_irem_m72_adapter
    irem_m72_adapter.cpp
)
add_library(
    mnemos::apps::player::irem_m72_adapter
    ALIAS mnemos_apps_player_irem_m72_adapter
)

target_include_directories(
    mnemos_apps_player_irem_m72_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_irem_m72_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::irem_m72
        mnemos::peripheral::common
        mnemos::runtime
        mnemos::apps::player::adapters::common
)

mnemos_apply_common_target_options(mnemos_apps_player_irem_m72_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_irem_m72_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_irem_m72
        mnemos_peripheral_common
        mnemos_runtime
        mnemos_apps_player_adapters_common
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_irem_m72_adapter_test
        SOURCES tests/irem_m72_adapter_test.cpp
        LIBRARIES mnemos::apps::player::irem_m72_adapter mnemos::io
    )
endif()
