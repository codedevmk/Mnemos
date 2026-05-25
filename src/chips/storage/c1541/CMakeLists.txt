add_library(
    mnemos_chips_storage_c1541
    d64_image.cpp
    synthetic_drive.cpp
    disk_bind.cpp
    full_drive.cpp
)
add_library(mnemos::chips::storage::c1541 ALIAS mnemos_chips_storage_c1541)

target_include_directories(
    mnemos_chips_storage_c1541
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_chips_storage_c1541
    PUBLIC
        mnemos::chips::shared
        mnemos::chips::cpu::m6510
        mnemos::chips::bus_controller::via_6522
)

mnemos_apply_common_target_options(mnemos_chips_storage_c1541 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_storage_c1541 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_storage_c1541_test
        SOURCES
            tests/d64_image_test.cpp
            tests/synthetic_drive_test.cpp
            tests/gcr_test.cpp
            tests/disk_bind_test.cpp
            tests/full_drive_test.cpp
        LIBRARIES mnemos::chips::storage::c1541
    )
endif()
