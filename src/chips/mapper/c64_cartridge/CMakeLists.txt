add_library(
    mnemos_chips_mapper_c64_cartridge
    c64_cartridge.cpp
)
add_library(mnemos::chips::mapper::c64_cartridge ALIAS mnemos_chips_mapper_c64_cartridge)

target_include_directories(
    mnemos_chips_mapper_c64_cartridge
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_mapper_c64_cartridge PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_mapper_c64_cartridge PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_mapper_c64_cartridge TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_mapper_c64_cartridge_test
        SOURCES tests/c64_cartridge_test.cpp
        LIBRARIES mnemos::chips::mapper::c64_cartridge
    )
endif()
