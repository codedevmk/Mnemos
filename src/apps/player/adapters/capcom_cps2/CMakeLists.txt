add_library(
    mnemos_apps_player_capcom_cps2_adapter
    capcom_cps2_adapter.cpp
)
add_library(
    mnemos::apps::player::capcom_cps2_adapter
    ALIAS mnemos_apps_player_capcom_cps2_adapter
)

target_include_directories(
    mnemos_apps_player_capcom_cps2_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_capcom_cps2_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::capcom_cps2
        mnemos::peripheral::common
        mnemos::runtime
        mnemos::apps::player::adapters::common
        mnemos::io
        mnemos::compression
        mnemos::security::cryptography
    PRIVATE
        mnemos::dsp
)

mnemos_apply_common_target_options(mnemos_apps_player_capcom_cps2_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_capcom_cps2_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_capcom_cps2
        mnemos_peripheral_common
        mnemos_runtime
        mnemos_apps_player_adapters_common
        mnemos_io
        mnemos_compression
        mnemos_security_cryptography
        mnemos_dsp
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_capcom_cps2_adapter_test
        SOURCES tests/capcom_cps2_adapter_test.cpp
        LIBRARIES mnemos::apps::player::capcom_cps2_adapter mnemos::dsp mnemos::io mnemos::graphics::images
    )
endif()
