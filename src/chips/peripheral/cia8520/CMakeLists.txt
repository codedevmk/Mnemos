add_library(
    mnemos_chips_peripheral_cia8520
    cia8520.cpp
)
add_library(mnemos::chips::peripheral::cia8520 ALIAS mnemos_chips_peripheral_cia8520)

target_include_directories(
    mnemos_chips_peripheral_cia8520
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_chips_peripheral_cia8520
    PUBLIC
        mnemos::chips::shared
        mnemos::chips::bus_controller::cia_6526
)

mnemos_apply_common_target_options(mnemos_chips_peripheral_cia8520 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_chips_peripheral_cia8520
    TIER 2
    DEPENDS mnemos_foundation mnemos_chips_bus_controller_cia_6526
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_peripheral_cia8520_test
        SOURCES tests/cia8520_test.cpp
        LIBRARIES mnemos::chips::peripheral::cia8520
    )
endif()
