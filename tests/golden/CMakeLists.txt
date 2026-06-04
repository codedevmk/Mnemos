# Golden boot test. Self-skips unless MNEMOS_C64_ROM_DIR points at real ROMs,
# so it is safe to run in CI (where it reports "skipped") and locally (where it
# boots and, with MNEMOS_C64_BOOT_SHA256 set, asserts the golden framebuffer).
mnemos_add_test(
    mnemos_c64_basic_boot_test
    SOURCES c64_basic_boot_test.cpp
    LIBRARIES
        mnemos::manifests::c64
        mnemos::runtime
        mnemos::foundation
        mnemos::security::cryptography
        mnemos::tools::runtime_cli
)
# ROMs are never committed; Catch2 exits 4 when the whole case skips, so CTest
# reports "Skipped" rather than failing when MNEMOS_C64_ROM_DIR is unset.
set_tests_properties(
    mnemos_c64_basic_boot_test
    PROPERTIES SKIP_RETURN_CODE 4
)

# SMS golden boot. Self-skips unless MNEMOS_SMS_ROM points at a .sms cartridge,
# so it is safe in CI (reports "Skipped") and locally boots + hashes the frame.
mnemos_add_test(
    mnemos_sms_boot_test
    SOURCES sms_boot_test.cpp
    LIBRARIES
        mnemos::manifests::sms
        mnemos::runtime
        mnemos::foundation
        mnemos::security::cryptography
        mnemos::tools::runtime_cli
)
set_tests_properties(
    mnemos_sms_boot_test
    PROPERTIES SKIP_RETURN_CODE 4
)

# Game Gear golden boot. Self-skips unless MNEMOS_GG_ROM points at a .gg
# cartridge, so it is safe in CI (reports "Skipped") and locally boots + hashes
# the cropped 160x144 LCD framebuffer.
mnemos_add_test(
    mnemos_gg_boot_test
    SOURCES gg_boot_test.cpp
    LIBRARIES
        mnemos::manifests::sms
        mnemos::runtime
        mnemos::foundation
        mnemos::security::cryptography
)
set_tests_properties(
    mnemos_gg_boot_test
    PROPERTIES SKIP_RETURN_CODE 4
)

# SMS Z80 conformance via the ZEXALL-SMS exerciser ROM. Self-skips unless
# MNEMOS_SMS_ZEXALL_ROM points at a zexall.sms image (GPLv2, never committed).
mnemos_add_test(
    mnemos_sms_zexall_test
    SOURCES sms_zexall_test.cpp
    LIBRARIES
        mnemos::manifests::sms
        mnemos::runtime
        mnemos::foundation
)
set_tests_properties(
    mnemos_sms_zexall_test
    PROPERTIES SKIP_RETURN_CODE 4
)

# Genesis golden boot. Self-skips unless MNEMOS_GENESIS_ROM points at a cartridge,
# so it is safe in CI (reports "Skipped") and locally boots + hashes the frame.
mnemos_add_test(
    mnemos_genesis_boot_test
    SOURCES genesis_boot_test.cpp
    LIBRARIES
        mnemos::manifests::genesis
        mnemos::runtime
        mnemos::foundation
        mnemos::tools::runtime_cli
)
set_tests_properties(
    mnemos_genesis_boot_test
    PROPERTIES SKIP_RETURN_CODE 4
)
