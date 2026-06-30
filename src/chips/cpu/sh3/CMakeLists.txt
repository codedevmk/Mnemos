add_library(
    mnemos_chips_cpu_sh3
    sh3.cpp
)
add_library(mnemos::chips::cpu::sh3 ALIAS mnemos_chips_cpu_sh3)

target_include_directories(
    mnemos_chips_cpu_sh3
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_chips_cpu_sh3
    PUBLIC
        mnemos::chips::cpu::sh2
)

mnemos_apply_common_target_options(mnemos_chips_cpu_sh3 PUBLIC)
mnemos_declare_tier(
    NAME mnemos_chips_cpu_sh3
    TIER 2
    DEPENDS
        mnemos_chips_cpu_sh2
        mnemos_foundation
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_sh3_test
        SOURCES tests/sh3_test.cpp
        LIBRARIES mnemos::chips::cpu::sh3 mnemos::topology
    )
endif()
