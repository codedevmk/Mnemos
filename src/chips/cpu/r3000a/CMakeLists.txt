add_library(
    mnemos_chips_cpu_r3000a
    r3000a.cpp
)
add_library(mnemos::chips::cpu::r3000a ALIAS mnemos_chips_cpu_r3000a)

target_include_directories(
    mnemos_chips_cpu_r3000a
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_r3000a PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_r3000a PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_r3000a TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_r3000a_test
        SOURCES tests/r3000a_test.cpp
        LIBRARIES mnemos::chips::cpu::r3000a
    )
endif()
