add_library(
    mnemos_manifests_sms
    sms_callbacks.cpp
    sms_system.cpp
    sms_region.cpp
)
add_library(mnemos::manifests::sms ALIAS mnemos_manifests_sms)

target_include_directories(
    mnemos_manifests_sms
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# The SMS assembler instantiates the concrete SMS chips directly, so it links them.
target_link_libraries(
    mnemos_manifests_sms
    PUBLIC
        mnemos::topology
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
endif()
