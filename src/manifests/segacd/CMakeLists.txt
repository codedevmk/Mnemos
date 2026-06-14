add_library(
    mnemos_manifests_segacd
    segacd_cdc.cpp
    segacd_cdd.cpp
    segacd_machine.cpp
    segacd_stamp.cpp
    segacd_system.cpp
)
add_library(mnemos::manifests::segacd ALIAS mnemos_manifests_segacd)

target_include_directories(
    mnemos_manifests_segacd
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The sub side instantiates the sub-CPU + PCM directly and owns a topology bus;
# segacd_machine joins it to a Genesis (manifests::genesis) on the main bus.
target_link_libraries(
    mnemos_manifests_segacd
    PUBLIC
        mnemos::topology
        mnemos::disc
        mnemos::manifests::genesis
        mnemos::chips::cpu::m68000
        mnemos::chips::audio::rf5c68
)

mnemos_apply_common_target_options(mnemos_manifests_segacd PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_segacd
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_manifests_genesis
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_segacd_test
        SOURCES
            tests/segacd_system_test.cpp
            tests/segacd_machine_test.cpp
            tests/segacd_save_state_test.cpp
        LIBRARIES mnemos::manifests::segacd
    )
endif()
