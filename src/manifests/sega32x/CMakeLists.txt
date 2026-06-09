add_library(
    mnemos_manifests_sega32x
    sega32x_system.cpp
)
add_library(mnemos::manifests::sega32x ALIAS mnemos_manifests_sega32x)

target_include_directories(
    mnemos_manifests_sega32x
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The 32X board instantiates two SH-2s on their own buses. The Genesis-bus
# bridge (sega32x_machine) joining it to manifests::genesis arrives in a later
# phase, so this skeleton depends only on the CPU + topology.
target_link_libraries(
    mnemos_manifests_sega32x
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::sh2
)

mnemos_apply_common_target_options(mnemos_manifests_sega32x PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_sega32x
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_sega32x_test
        SOURCES tests/sega32x_system_test.cpp
        LIBRARIES mnemos::manifests::sega32x
    )
endif()
