add_library(
    mnemos_chips_bus_controller_via_6522
    via_6522.cpp
)
add_library(mnemos::chips::bus_controller::via_6522 ALIAS mnemos_chips_bus_controller_via_6522)

target_include_directories(
    mnemos_chips_bus_controller_via_6522
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_bus_controller_via_6522 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_bus_controller_via_6522 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_bus_controller_via_6522 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_bus_controller_via_6522_test
        SOURCES tests/via_6522_test.cpp
        LIBRARIES mnemos::chips::bus_controller::via_6522
    )
endif()
