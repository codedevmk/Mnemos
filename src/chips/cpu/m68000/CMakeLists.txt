add_library(
    mnemos_chips_cpu_m68000
    m68000.cpp
)
add_library(mnemos::chips::cpu::m68000 ALIAS mnemos_chips_cpu_m68000)

target_include_directories(
    mnemos_chips_cpu_m68000
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_m68000 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_m68000 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_m68000 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_m68000_test
        SOURCES tests/m68000_test.cpp
        LIBRARIES mnemos::chips::cpu::m68000 mnemos::topology
    )

    mnemos_fetch_json()
    mnemos_add_test(
        mnemos_chips_cpu_m68000_singlestep_test
        SOURCES tests/m68000_singlestep_test.cpp
        LIBRARIES mnemos::chips::cpu::m68000 nlohmann_json::nlohmann_json
    )
    # The corpus is never committed; Catch2 exits 4 when the whole case skips, so
    # CTest reports "Skipped" when MNEMOS_M68000_TESTS_DIR is unset.
    set_tests_properties(
        mnemos_chips_cpu_m68000_singlestep_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
