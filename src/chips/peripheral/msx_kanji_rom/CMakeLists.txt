add_library(
    mnemos_chips_peripheral_msx_kanji_rom
    msx_kanji_rom.cpp
)
add_library(
    mnemos::chips::peripheral::msx_kanji_rom
    ALIAS mnemos_chips_peripheral_msx_kanji_rom
)

target_include_directories(
    mnemos_chips_peripheral_msx_kanji_rom
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_peripheral_msx_kanji_rom PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_peripheral_msx_kanji_rom PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_peripheral_msx_kanji_rom TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_peripheral_msx_kanji_rom_test
        SOURCES tests/msx_kanji_rom_test.cpp
        LIBRARIES mnemos::chips::peripheral::msx_kanji_rom
    )
endif()
