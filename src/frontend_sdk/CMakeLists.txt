add_library(mnemos_frontend_sdk STATIC adapter_registry.cpp)
add_library(mnemos::frontend_sdk ALIAS mnemos_frontend_sdk)

target_include_directories(
    mnemos_frontend_sdk
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_frontend_sdk
    PUBLIC
        mnemos::foundation
        mnemos::chips::shared
        mnemos::instrumentation
        mnemos::peripheral::common
)

mnemos_apply_common_target_options(mnemos_frontend_sdk PUBLIC)
mnemos_declare_tier(
    NAME mnemos_frontend_sdk
    TIER 7
    DEPENDS
        mnemos_foundation
        mnemos_chips_shared
        mnemos_instrumentation
        mnemos_peripheral_common
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_frontend_sdk_test
        SOURCES
            tests/player_system_test.cpp
            tests/adapter_registry_test.cpp
        LIBRARIES mnemos::frontend_sdk
    )
endif()
