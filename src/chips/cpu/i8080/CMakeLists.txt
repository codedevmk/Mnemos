add_library(
    mnemos_chips_cpu_i8080
    i8080.cpp
)
add_library(mnemos::chips::cpu::i8080 ALIAS mnemos_chips_cpu_i8080)

target_include_directories(
    mnemos_chips_cpu_i8080
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_i8080 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_i8080 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_i8080 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_i8080_test
        SOURCES tests/i8080_test.cpp
        LIBRARIES mnemos::chips::cpu::i8080 mnemos::topology
    )
endif()
