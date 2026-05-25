add_library(
    mnemos_topology
    bus.cpp
)
add_library(mnemos::topology ALIAS mnemos_topology)

target_include_directories(
    mnemos_topology
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_topology PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_topology PUBLIC)
mnemos_declare_tier(NAME mnemos_topology TIER 3 DEPENDS mnemos_chips_shared mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_topology_test
        SOURCES tests/bus_test.cpp
        LIBRARIES mnemos::topology
    )
endif()
