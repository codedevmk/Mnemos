add_library(
    mnemos_chips_storage_wd1793
    wd1793.cpp
)
add_library(mnemos::chips::storage::wd1793 ALIAS mnemos_chips_storage_wd1793)

target_include_directories(
    mnemos_chips_storage_wd1793
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_storage_wd1793 PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_storage_wd1793 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_storage_wd1793 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_storage_wd1793_test
        SOURCES tests/wd1793_test.cpp
        LIBRARIES mnemos::chips::storage::wd1793
    )
endif()
