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
    PRIVATE
        mnemos::compression
        mnemos::io
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
        mnemos_compression
        mnemos_io
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_irem_m72_adapter_test
        SOURCES tests/irem_m72_adapter_test.cpp
        LIBRARIES
            mnemos::apps::player::irem_m72_adapter
            mnemos::compression
            mnemos::io
            mnemos::security::cryptography
        ARGS "~[data]"
    )

    add_test(
        NAME mnemos_apps_player_irem_m72_rtype_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter boots the real first-game set"
    )
    add_test(
        NAME mnemos_apps_player_irem_m72_protected_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter boots a real protected M72 set"
    )
    add_test(
        NAME mnemos_apps_player_irem_m72_protected_audio_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter emits rendered audio for a real protected M72 set"
    )
    add_test(
        NAME mnemos_apps_player_irem_m72_protected_mcu_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter boots a real dumped-MCU protected M72 set"
    )
    add_test(
        NAME mnemos_apps_player_irem_m72_parity_hash_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter matches optional visual/audio parity hashes for a real M72 set"
    )
    add_test(
        NAME mnemos_apps_player_irem_m72_vertical_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter validates a real vertical M72 set orientation"
    )
    add_test(
        NAME mnemos_apps_player_irem_m72_roster_golden_test
        COMMAND
            mnemos_apps_player_irem_m72_adapter_test
            "irem_m72_adapter validates the checked-in true-M72 ROM roster"
    )
    set_tests_properties(
        mnemos_apps_player_irem_m72_rtype_golden_test
        mnemos_apps_player_irem_m72_protected_golden_test
        mnemos_apps_player_irem_m72_protected_audio_golden_test
        mnemos_apps_player_irem_m72_protected_mcu_golden_test
        mnemos_apps_player_irem_m72_parity_hash_golden_test
        mnemos_apps_player_irem_m72_vertical_golden_test
        mnemos_apps_player_irem_m72_roster_golden_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
