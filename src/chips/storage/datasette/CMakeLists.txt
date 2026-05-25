add_library(
    mnemos_chips_storage_datasette
    datasette.cpp
)
add_library(mnemos::chips::storage::datasette ALIAS mnemos_chips_storage_datasette)

target_include_directories(
    mnemos_chips_storage_datasette
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_storage_datasette PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_storage_datasette PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_storage_datasette TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_storage_datasette_test
        SOURCES tests/datasette_test.cpp
        LIBRARIES mnemos::chips::storage::datasette
    )
endif()
