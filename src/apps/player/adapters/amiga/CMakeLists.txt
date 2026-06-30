add_library(
    mnemos_apps_player_amiga_adapter
    amiga_adapter.cpp
)
add_library(
    mnemos::apps::player::amiga_adapter
    ALIAS mnemos_apps_player_amiga_adapter
)

target_include_directories(
    mnemos_apps_player_amiga_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_amiga_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::amiga
        mnemos::runtime
        mnemos::apps::player::adapters::common
    PRIVATE
        mnemos::dsp
)

mnemos_apply_common_target_options(mnemos_apps_player_amiga_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_amiga_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_amiga
        mnemos_runtime
        mnemos_apps_player_adapters_common
        mnemos_dsp
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_amiga_adapter_test
        SOURCES tests/amiga_adapter_test.cpp
        LIBRARIES mnemos::apps::player::amiga_adapter
    )
endif()
