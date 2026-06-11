add_library(
    mnemos_chips_cpu_mcs51
    mcs51.cpp
)
add_library(mnemos::chips::cpu::mcs51 ALIAS mnemos_chips_cpu_mcs51)

target_include_directories(
    mnemos_chips_cpu_mcs51
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_mcs51 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_mcs51 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_mcs51 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_mcs51_test
        SOURCES tests/mcs51_test.cpp
        LIBRARIES mnemos::chips::cpu::mcs51
    )
endif()
