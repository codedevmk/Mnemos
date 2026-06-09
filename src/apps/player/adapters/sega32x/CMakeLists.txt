add_library(
    mnemos_apps_player_sega32x_adapter
    sega32x_adapter.cpp
)
add_library(
    mnemos::apps::player::sega32x_adapter
    ALIAS mnemos_apps_player_sega32x_adapter
)

target_include_directories(
    mnemos_apps_player_sega32x_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_sega32x_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::sega32x
        mnemos::peripheral::input
        mnemos::runtime
        mnemos::apps::player::adapters::common
    PRIVATE
        mnemos::dsp
)

mnemos_apply_common_target_options(mnemos_apps_player_sega32x_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_sega32x_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_sega32x
        mnemos_peripheral_input
        mnemos_runtime
        mnemos_apps_player_adapters_common
        mnemos_dsp
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_sega32x_adapter_test
        SOURCES tests/sega32x_adapter_test.cpp
        LIBRARIES mnemos::apps::player::sega32x_adapter
    )
    # The boot golden needs the real 32X boot ROMs + a cartridge; it SKIPs
    # (exit 4) when MNEMOS_32X_BIOS_DIR / MNEMOS_32X_ROM are unset.
    set_tests_properties(
        mnemos_apps_player_sega32x_adapter_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
