add_library(
    mnemos_chips_cpu_sh2
    sh2.cpp
    sh2_peripherals.cpp
)
add_library(mnemos::chips::cpu::sh2 ALIAS mnemos_chips_cpu_sh2)

target_include_directories(
    mnemos_chips_cpu_sh2
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_sh2 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_sh2 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_sh2 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_sh2_test
        SOURCES
            tests/sh2_test.cpp
            tests/sh2_divu_test.cpp
        LIBRARIES mnemos::chips::cpu::sh2 mnemos::topology
    )

    # Functional conformance against the public SH4 single-step corpus (see
    # THIRD-PARTY-REFERENCES.md). The corpus is never committed; the test SKIPs
    # (Catch2 exits 4) unless MNEMOS_SH2_TESTS_DIR points at the transcoded JSON.
    mnemos_add_test(
        mnemos_chips_cpu_sh2_conformance_test
        SOURCES tests/sh2_conformance_test.cpp
        LIBRARIES mnemos::chips::cpu::sh2 mnemos::topology nlohmann_json::nlohmann_json
    )
    set_tests_properties(
        mnemos_chips_cpu_sh2_conformance_test
        PROPERTIES SKIP_RETURN_CODE 4
    )

    # Cycle conformance against the manual-derived corpus (ADR-0026): authority is
    # the SH7600/SH7604 + 32X manuals; Emu/Ymir are L5 cross-check only. The corpus
    # is never committed; the test SKIPs (Catch2 exits 4) unless
    # MNEMOS_SH2_CYCLE_TESTS_DIR points at the authored JSON vectors.
    mnemos_add_test(
        mnemos_chips_cpu_sh2_cycle_conformance_test
        SOURCES tests/sh2_cycle_conformance_test.cpp
        LIBRARIES mnemos::chips::cpu::sh2 mnemos::topology nlohmann_json::nlohmann_json
    )
    set_tests_properties(
        mnemos_chips_cpu_sh2_cycle_conformance_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
