add_library(
    mnemos_chips_storage_msx_cassette
    msx_cassette.cpp
)
add_library(mnemos::chips::storage::msx_cassette ALIAS mnemos_chips_storage_msx_cassette)

target_include_directories(
    mnemos_chips_storage_msx_cassette
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_storage_msx_cassette PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_storage_msx_cassette PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_storage_msx_cassette TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_storage_msx_cassette_test
        SOURCES tests/msx_cassette_test.cpp
        LIBRARIES mnemos::chips::storage::msx_cassette
    )
endif()
