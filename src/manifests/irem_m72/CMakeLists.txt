add_library(
    mnemos_manifests_irem_m72
    m72_system.cpp
)
add_library(mnemos::manifests::irem_m72 ALIAS mnemos_manifests_irem_m72)

target_include_directories(
    mnemos_manifests_irem_m72
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The M72 board: V30 + Z80 on their own buses, ROM regions out of the arcade
# ROM-set loader (manifests::common).
target_link_libraries(
    mnemos_manifests_irem_m72
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::v30
        mnemos::chips::cpu::z80
        mnemos::chips::cpu::mcs51
        mnemos::chips::video::irem_m72_video
        mnemos::chips::audio::ym2151
        mnemos::chips::audio::dac8
        mnemos::manifests::common
)

mnemos_apply_common_target_options(mnemos_manifests_irem_m72 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_irem_m72
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_manifests_common
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_irem_m72_test
        SOURCES tests/m72_system_test.cpp
        LIBRARIES mnemos::manifests::irem_m72 mnemos::runtime
    )
endif()
