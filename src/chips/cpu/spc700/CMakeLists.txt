add_library(
    mnemos_chips_cpu_spc700
    spc700.cpp
)
add_library(mnemos::chips::cpu::spc700 ALIAS mnemos_chips_cpu_spc700)

target_include_directories(
    mnemos_chips_cpu_spc700
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_spc700 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_spc700 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_spc700 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_spc700_test
        SOURCES tests/spc700_test.cpp
        LIBRARIES mnemos::chips::cpu::spc700 mnemos::topology
    )
endif()
