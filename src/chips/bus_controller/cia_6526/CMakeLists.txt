add_library(
    mnemos_chips_bus_controller_cia_6526
    src/cia_6526.cpp
)
add_library(mnemos::chips::bus_controller::cia_6526 ALIAS mnemos_chips_bus_controller_cia_6526)

target_include_directories(
    mnemos_chips_bus_controller_cia_6526
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_bus_controller_cia_6526 PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_bus_controller_cia_6526 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_bus_controller_cia_6526 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_bus_controller_cia_6526_test
        SOURCES tests/cia_6526_test.cpp
        LIBRARIES mnemos::chips::bus_controller::cia_6526
    )
endif()
