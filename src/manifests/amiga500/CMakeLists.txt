add_library(
    mnemos_manifests_amiga500
    amiga500_system.cpp
)
add_library(mnemos::manifests::amiga500 ALIAS mnemos_manifests_amiga500)

target_include_directories(
    mnemos_manifests_amiga500
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_amiga500
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::m68000
        mnemos::chips::video::agnus
        mnemos::chips::video::denise
        mnemos::chips::audio::paula
        mnemos::chips::peripheral::cia8520
)

mnemos_apply_common_target_options(mnemos_manifests_amiga500 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_amiga500
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_amiga500_test
        SOURCES tests/amiga500_system_test.cpp
        LIBRARIES mnemos::manifests::amiga500 mnemos::runtime
    )
endif()
