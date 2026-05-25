add_library(
    mnemos_chips_mapper_c64_pla
    src/c64_pla.cpp
)
add_library(mnemos::chips::mapper::c64_pla ALIAS mnemos_chips_mapper_c64_pla)

target_include_directories(
    mnemos_chips_mapper_c64_pla
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_mapper_c64_pla PUBLIC mnemos::chips::common)

mnemos_apply_common_target_options(mnemos_chips_mapper_c64_pla PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_mapper_c64_pla TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_mapper_c64_pla_test
        SOURCES tests/c64_pla_test.cpp
        LIBRARIES mnemos::chips::mapper::c64_pla
    )
endif()
