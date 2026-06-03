add_library(
    mnemos_chips_mapper_hicom_mapper
    hicom_mapper.cpp
)
add_library(mnemos::chips::mapper::hicom_mapper ALIAS mnemos_chips_mapper_hicom_mapper)

target_include_directories(
    mnemos_chips_mapper_hicom_mapper
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_mapper_hicom_mapper PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_mapper_hicom_mapper PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_mapper_hicom_mapper TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_mapper_hicom_mapper_test
        SOURCES tests/hicom_mapper_test.cpp
        LIBRARIES mnemos::chips::mapper::hicom_mapper
    )
endif()
