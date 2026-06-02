add_library(
    mnemos_chips_storage_eeprom_i2c
    eeprom_i2c.cpp
)
add_library(mnemos::chips::storage::eeprom_i2c ALIAS mnemos_chips_storage_eeprom_i2c)

target_include_directories(
    mnemos_chips_storage_eeprom_i2c
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_chips_storage_eeprom_i2c PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_storage_eeprom_i2c TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_storage_eeprom_i2c_test
        SOURCES tests/eeprom_i2c_test.cpp
        LIBRARIES mnemos::chips::storage::eeprom_i2c
    )
endif()
