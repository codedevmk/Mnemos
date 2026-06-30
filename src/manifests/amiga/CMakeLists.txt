add_library(
    mnemos_manifests_amiga
    amiga_system.cpp
    chipsets/amiga_chipsets.cpp
    devices/amiga_keyboard.cpp
    devices/amiga_input.cpp
    drives/amiga_floppy.cpp
    expansions/zorro2.cpp
    models/amiga_models.cpp
)
add_library(mnemos::manifests::amiga ALIAS mnemos_manifests_amiga)

target_include_directories(
    mnemos_manifests_amiga
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_manifests_amiga
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::m68000
        mnemos::chips::video::agnus
        mnemos::chips::video::denise
        mnemos::chips::audio::paula
        mnemos::chips::peripheral::cia8520
)

mnemos_apply_common_target_options(mnemos_manifests_amiga PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_amiga
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_amiga_test
        SOURCES tests/amiga_system_test.cpp
        LIBRARIES mnemos::manifests::amiga mnemos::runtime
    )
endif()
