add_library(
    mnemos_chips_cpu_m6510
    src/m6510.cpp
)
add_library(mnemos::chips::cpu::m6510 ALIAS mnemos_chips_cpu_m6510)

target_include_directories(
    mnemos_chips_cpu_m6510
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_m6510 PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_cpu_m6510 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_m6510 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_m6510_test
        SOURCES tests/m6510_test.cpp
        LIBRARIES mnemos::chips::cpu::m6510
    )
endif()
