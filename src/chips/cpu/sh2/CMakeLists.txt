add_library(
    mnemos_chips_cpu_sh2
    sh2.cpp
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
        SOURCES tests/sh2_test.cpp
        LIBRARIES mnemos::chips::cpu::sh2 mnemos::topology
    )
endif()
