add_library(
    mnemos_chips_shared
    chip_registry.cpp
    state.cpp
)
add_library(mnemos::chips::shared ALIAS mnemos_chips_shared)

target_include_directories(
    mnemos_chips_shared
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_shared PUBLIC mnemos::foundation)

mnemos_apply_common_target_options(mnemos_chips_shared PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_shared TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_shared_test
        SOURCES
            tests/chip_registry_test.cpp
            tests/state_test.cpp
            tests/iec_bus_test.cpp
            tests/introspection_views_test.cpp
            tests/callbacks_test.cpp
        LIBRARIES mnemos::chips::shared
    )
endif()
