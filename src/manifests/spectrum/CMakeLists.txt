add_library(
    mnemos_manifests_spectrum
    spectrum_snapshot.cpp
    spectrum_system.cpp
)
add_library(mnemos::manifests::spectrum ALIAS mnemos_manifests_spectrum)

target_include_directories(
    mnemos_manifests_spectrum
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The Spectrum assembler instantiates the concrete Z80 + ULA directly + wires the
# topology bus, so it links them.
target_link_libraries(
    mnemos_manifests_spectrum
    PUBLIC
        mnemos::topology
        mnemos::chips::cpu::z80
        mnemos::chips::video::ula
        mnemos::chips::audio::beeper
        mnemos::chips::audio::ssg
)

mnemos_apply_common_target_options(mnemos_manifests_spectrum PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_spectrum
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_chips_shared
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_spectrum_test
        SOURCES tests/spectrum_system_test.cpp
        LIBRARIES mnemos::manifests::spectrum
    )
endif()
