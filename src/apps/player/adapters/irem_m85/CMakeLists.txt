add_library(
    mnemos_apps_player_irem_m85_adapter
    irem_m85_adapter.cpp
)
add_library(
    mnemos::apps::player::irem_m85_adapter
    ALIAS mnemos_apps_player_irem_m85_adapter
)

target_include_directories(
    mnemos_apps_player_irem_m85_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_apps_player_irem_m85_adapter
    PUBLIC
        mnemos::frontend_sdk
        mnemos::manifests::irem_m85
        mnemos::peripheral::common
        mnemos::runtime
        mnemos::apps::player::adapters::common
    PRIVATE
        mnemos::compression
        mnemos::io
)

mnemos_apply_common_target_options(mnemos_apps_player_irem_m85_adapter PUBLIC)
mnemos_declare_tier(
    NAME mnemos_apps_player_irem_m85_adapter
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_manifests_irem_m85
        mnemos_peripheral_common
        mnemos_runtime
        mnemos_apps_player_adapters_common
        mnemos_compression
        mnemos_io
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apps_player_irem_m85_adapter_test
        SOURCES tests/irem_m85_adapter_test.cpp
        LIBRARIES mnemos::apps::player::irem_m85_adapter mnemos::compression mnemos::io
        ARGS "~[data]"
    )

    add_test(
        NAME mnemos_apps_player_irem_m85_corpus_golden_test
        COMMAND
            mnemos_apps_player_irem_m85_adapter_test
            "irem_m85_adapter validates real M85 ROM sets"
    )
    set_tests_properties(
        mnemos_apps_player_irem_m85_corpus_golden_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
