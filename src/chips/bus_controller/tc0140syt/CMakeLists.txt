add_library(
    mnemos_chips_bus_controller_tc0140syt
    tc0140syt.cpp
)
add_library(mnemos::chips::bus_controller::tc0140syt ALIAS mnemos_chips_bus_controller_tc0140syt)

target_include_directories(
    mnemos_chips_bus_controller_tc0140syt
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_bus_controller_tc0140syt PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_bus_controller_tc0140syt PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_bus_controller_tc0140syt TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_bus_controller_tc0140syt_test
        SOURCES tests/tc0140syt_test.cpp
        LIBRARIES mnemos::chips::bus_controller::tc0140syt
    )
endif()
