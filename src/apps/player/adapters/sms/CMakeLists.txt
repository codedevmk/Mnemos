add_library(
    mnemos_apps_player_sms_adapter
    sms_adapter.cpp
)
add_library(
    mnemos::apps::player::sms_adapter
    ALIAS mnemos_apps_player_sms_adapter
)

target_include_directories(
    mnemos_apps_player_sms_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_sms_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::sms
        mnemos::runtime
)

mnemos_apply_common_target_options(mnemos_apps_player_sms_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_sms_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_sms
        mnemos_runtime
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_sms_adapter_test
        SOURCES tests/sms_adapter_test.cpp
        LIBRARIES mnemos::apps::player::sms_adapter
    )
endif()
