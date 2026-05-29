# Embed the canonical SMS manifests as compiled string constants so the player
# carries them (no runtime filesystem dependency) and tests share the exact same
# bytes. Editing any source .toml re-runs configuration and regenerates the
# header, so the embedded copy can never drift from the file.
set(_sms_manifest_files
    "sms.ntsc.toml"
    "sms.pal.toml"
    "sms.ntsc.codemasters.toml"
    "sms.pal.codemasters.toml"
)
foreach(_f IN LISTS _sms_manifest_files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}"
    )
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sms.ntsc.toml" MNEMOS_SMS_NTSC_TOML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sms.pal.toml" MNEMOS_SMS_PAL_TOML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sms.ntsc.codemasters.toml"
     MNEMOS_SMS_NTSC_CODEMASTERS_TOML)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sms.pal.codemasters.toml"
     MNEMOS_SMS_PAL_CODEMASTERS_TOML)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/sms_embedded_manifests.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sms_embedded_manifests.hpp"
    @ONLY
)

add_library(
    mnemos_manifests_sms
    sms_callbacks.cpp
    sms_runtime.cpp
    sms_system.cpp
    sms_region.cpp
)
add_library(mnemos::manifests::sms ALIAS mnemos_manifests_sms)

target_include_directories(
    mnemos_manifests_sms
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The SMS assembler instantiates the concrete SMS chips directly, so it links them.
target_link_libraries(
    mnemos_manifests_sms
    PUBLIC
        mnemos::topology
        mnemos::manifests::common
        mnemos::chips::cpu::z80
        mnemos::chips::video::sms_vdp
        mnemos::chips::audio::sn76489
        mnemos::chips::mapper::sms_mapper
        mnemos::chips::mapper::codemasters_mapper
        mnemos::peripheral::input
)

mnemos_apply_common_target_options(mnemos_manifests_sms PUBLIC)
mnemos_declare_tier(
    NAME mnemos_manifests_sms
    TIER 4
    DEPENDS
        mnemos_topology
        mnemos_manifests_common
        mnemos_chips_shared
        mnemos_foundation
        mnemos_peripheral_common
        mnemos_peripheral_input
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_manifests_sms_test
        SOURCES tests/sms_system_test.cpp
        LIBRARIES mnemos::manifests::sms
    )
    mnemos_add_test(
        mnemos_manifests_sms_region_test
        SOURCES tests/sms_region_test.cpp
        LIBRARIES mnemos::manifests::sms
    )
    mnemos_add_test(
        mnemos_manifests_sms_manifest_path_test
        SOURCES tests/sms_manifest_path_test.cpp
        LIBRARIES mnemos::manifests::sms mnemos::manifests
    )
    mnemos_add_test(
        mnemos_manifests_sms_manifest_parity_test
        SOURCES tests/sms_manifest_parity_test.cpp
        LIBRARIES mnemos::manifests::sms mnemos::manifests mnemos::runtime
    )
    # Data-gated on MNEMOS_SMS_BIOS=<absolute path>. Catch2 returns 4 from a
    # SKIP'd case; mark the test so ctest reports "Skipped" rather than
    # "Failed" when the env var is unset.
    set_tests_properties(
        mnemos_manifests_sms_manifest_parity_test
        PROPERTIES SKIP_RETURN_CODE 4
    )
    # Codemasters-mapper manifest parity. The synthetic-cart cases always run;
    # the real-cart case is data-gated on MNEMOS_SMS_CODIES_ROM and SKIPs when
    # unset (the binary still returns 0 because the synthetic cases run).
    mnemos_add_test(
        mnemos_manifests_sms_codemasters_parity_test
        SOURCES tests/sms_codemasters_parity_test.cpp
        LIBRARIES mnemos::manifests::sms mnemos::manifests mnemos::runtime
    )
    # Integration parity for the build_sms_runtime wrapper vs assemble_sms. The
    # synthetic-cart cases always run; the real-BIOS case is data-gated on
    # MNEMOS_SMS_BIOS and SKIPs when unset (synthetic cases keep the binary at 0).
    mnemos_add_test(
        mnemos_manifests_sms_runtime_parity_test
        SOURCES tests/sms_runtime_parity_test.cpp
        LIBRARIES mnemos::manifests::sms mnemos::manifests mnemos::runtime
    )
endif()
