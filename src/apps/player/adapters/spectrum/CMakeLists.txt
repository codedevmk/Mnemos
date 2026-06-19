add_library(
    mnemos_apps_player_spectrum_adapter
    spectrum_adapter.cpp
)
add_library(
    mnemos::apps::player::spectrum_adapter
    ALIAS mnemos_apps_player_spectrum_adapter
)

target_include_directories(
    mnemos_apps_player_spectrum_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_spectrum_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::spectrum
        mnemos::runtime
        mnemos::apps::player::adapters::common
)

mnemos_apply_common_target_options(mnemos_apps_player_spectrum_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_spectrum_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_spectrum
        mnemos_runtime
        mnemos_apps_player_adapters_common
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_spectrum_adapter_test
        SOURCES tests/spectrum_adapter_test.cpp
        LIBRARIES mnemos::apps::player::spectrum_adapter
    )
endif()
