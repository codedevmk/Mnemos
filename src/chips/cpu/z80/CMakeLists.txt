add_library(
    mnemos_chips_cpu_z80
    src/z80.cpp
)
add_library(mnemos::chips::cpu::z80 ALIAS mnemos_chips_cpu_z80)

target_include_directories(
    mnemos_chips_cpu_z80
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_z80 PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_cpu_z80 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_z80 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_z80_test
        SOURCES tests/z80_test.cpp
        LIBRARIES mnemos::chips::cpu::z80 mnemos::topology
    )
endif()
