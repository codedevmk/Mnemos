add_library(
    mnemos_chips_common
    src/chip_registry.cpp
    src/state.cpp
)
add_library(mnemos::chips::common ALIAS mnemos_chips_common)

target_include_directories(
    mnemos_chips_common
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_common PUBLIC mnemos::foundation)

mnemos_apply_common_target_options(mnemos_chips_common PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_common TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_common_test
        SOURCES
            tests/chip_registry_test.cpp
            tests/state_test.cpp
            tests/iec_bus_test.cpp
        LIBRARIES mnemos::chips::common
    )
endif()
