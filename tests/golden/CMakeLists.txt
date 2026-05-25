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
        mnemos::tools::runtime_cli
)
# ROMs are never committed; Catch2 exits 4 when the whole case skips, so CTest
# reports "Skipped" rather than failing when MNEMOS_C64_ROM_DIR is unset.
set_tests_properties(
    mnemos_c64_basic_boot_test
    PROPERTIES SKIP_RETURN_CODE 4
)
