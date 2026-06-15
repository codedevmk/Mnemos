add_library(
    mnemos_chips_cpu_scu_dsp
    scu_dsp.cpp
)
add_library(mnemos::chips::cpu::scu_dsp ALIAS mnemos_chips_cpu_scu_dsp)

target_include_directories(
    mnemos_chips_cpu_scu_dsp
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_scu_dsp PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_scu_dsp PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_scu_dsp TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_scu_dsp_test
        SOURCES tests/scu_dsp_test.cpp
        LIBRARIES mnemos::chips::cpu::scu_dsp mnemos::topology
    )
endif()
