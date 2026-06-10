add_library(
    mnemos_chips_cpu_v30
    v30.cpp
)
add_library(mnemos::chips::cpu::v30 ALIAS mnemos_chips_cpu_v30)

target_include_directories(
    mnemos_chips_cpu_v30
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_v30 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_v30 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_v30 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_v30_test
        SOURCES tests/v30_test.cpp
        LIBRARIES mnemos::chips::cpu::v30
    )

    # Functional conformance against a public per-instruction 8088/V20 JSON
    # corpus (not committed). Skips unless MNEMOS_V30_TESTS_DIR is set; Catch2
    # exits 4 when the case skips.
    mnemos_fetch_json()
    mnemos_add_test(
        mnemos_chips_cpu_v30_singlestep_test
        SOURCES tests/v30_singlestep_test.cpp
        LIBRARIES mnemos::chips::cpu::v30 nlohmann_json::nlohmann_json
    )
    set_tests_properties(
        mnemos_chips_cpu_v30_singlestep_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
endif()
