add_library(
    mnemos_chips_cpu_m6803
    m6803.cpp
)
add_library(mnemos::chips::cpu::m6803 ALIAS mnemos_chips_cpu_m6803)

target_include_directories(
    mnemos_chips_cpu_m6803
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_m6803 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_m6803 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_m6803 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_m6803_test
        SOURCES tests/m6803_test.cpp
        LIBRARIES mnemos::chips::cpu::m6803 mnemos::topology
    )
endif()
