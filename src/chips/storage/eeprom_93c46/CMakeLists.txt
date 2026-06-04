add_library(
    mnemos_chips_storage_eeprom_93c46
    eeprom_93c46.cpp
)
add_library(mnemos::chips::storage::eeprom_93c46 ALIAS mnemos_chips_storage_eeprom_93c46)

target_include_directories(
    mnemos_chips_storage_eeprom_93c46
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_chips_storage_eeprom_93c46 PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_storage_eeprom_93c46 TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_storage_eeprom_93c46_test
        SOURCES tests/eeprom_93c46_test.cpp
        LIBRARIES mnemos::chips::storage::eeprom_93c46
    )
endif()
