add_library(
    mnemos_chips_cpu_z80
    z80.cpp
)
add_library(mnemos::chips::cpu::z80 ALIAS mnemos_chips_cpu_z80)

target_include_directories(
    mnemos_chips_cpu_z80
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_z80 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_z80 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_z80 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_z80_test
        SOURCES tests/z80_test.cpp
        LIBRARIES mnemos::chips::cpu::z80 mnemos::topology
    )

    # Data-gated Z80 conformance: skips unless MNEMOS_Z80_TEST_ROM points at a
    # CP/M .com exerciser (not committed). Catch2 exits 4 when the case skips.
    mnemos_add_test(
        mnemos_chips_cpu_z80_conformance_test
        SOURCES tests/z80_conformance_test.cpp
        LIBRARIES mnemos::chips::cpu::z80 mnemos::topology mnemos::io
    )
    set_tests_properties(
        mnemos_chips_cpu_z80_conformance_test
        PROPERTIES SKIP_RETURN_CODE 4
    )

    # Per-cycle bus-trace conformance against a public Z80 per-instruction JSON
    # corpus. Complements the CP/M exerciser above: zexall pins functional
    # correctness via CRCs, this pins cycle accuracy + access order. Data-gated
    # via MNEMOS_Z80_TESTS_DIR.
    mnemos_fetch_json()
    mnemos_add_test(
        mnemos_chips_cpu_z80_singlestep_test
        SOURCES tests/z80_singlestep_test.cpp
        LIBRARIES mnemos::chips::cpu::z80 nlohmann_json::nlohmann_json
    )
    set_tests_properties(
        mnemos_chips_cpu_z80_singlestep_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
