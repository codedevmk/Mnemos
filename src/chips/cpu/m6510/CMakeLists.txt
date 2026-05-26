add_library(
    mnemos_chips_cpu_m6510
    m6510.cpp
    decode_table.cpp
)
add_library(mnemos::chips::cpu::m6510 ALIAS mnemos_chips_cpu_m6510)

target_include_directories(
    mnemos_chips_cpu_m6510
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_m6510 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_m6510 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_m6510 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_m6510_test
        SOURCES tests/m6510_test.cpp
        LIBRARIES mnemos::chips::cpu::m6510
    )

    mnemos_fetch_json()
    mnemos_add_test(
        mnemos_chips_cpu_m6510_conformance_test
        SOURCES tests/m6510_conformance_test.cpp
        LIBRARIES mnemos::chips::cpu::m6510 nlohmann_json::nlohmann_json
    )
    # The corpus is never committed; Catch2 exits 4 when the whole case skips.
    # This case has only two assertions, so 4 unambiguously means "data absent".
    set_tests_properties(
        mnemos_chips_cpu_m6510_conformance_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
