add_library(
    mnemos_chips_cpu_ssp1601
    ssp1601.cpp
)
add_library(mnemos::chips::cpu::ssp1601 ALIAS mnemos_chips_cpu_ssp1601)

target_include_directories(
    mnemos_chips_cpu_ssp1601
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_cpu_ssp1601 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_cpu_ssp1601 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_cpu_ssp1601 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_cpu_ssp1601_test
        SOURCES tests/ssp1601_test.cpp
        LIBRARIES mnemos::chips::cpu::ssp1601
    )
endif()
