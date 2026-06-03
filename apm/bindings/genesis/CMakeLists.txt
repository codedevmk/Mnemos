# Genesis engine binding: wraps build_genesis_runtime behind the APM plugin C ABI.
# Built as a SHARED library named "mnemos_genesis" that the tracer LoadLibrary's.
# The engine proper is untouched -- this binding is the single bridge, and it lives
# entirely under apm/.
add_library(mnemos_apm_genesis_binding SHARED genesis_binding.cpp)
add_library(mnemos::apm::genesis_binding ALIAS mnemos_apm_genesis_binding)
set_target_properties(mnemos_apm_genesis_binding PROPERTIES OUTPUT_NAME "mnemos_genesis")

target_link_libraries(
    mnemos_apm_genesis_binding
    PRIVATE
        mnemos::apm::abi
        mnemos::manifests::genesis
        mnemos::runtime
)

mnemos_apply_common_target_options(mnemos_apm_genesis_binding PRIVATE)

if(BUILD_TESTING)
    # Compile the binding source straight into the test so the C ABI is exercised
    # in-process against a synthetic ROM (no DLL load, no game ROM needed).
    mnemos_add_test(
        mnemos_apm_genesis_binding_test
        SOURCES tests/genesis_binding_test.cpp genesis_binding.cpp
        LIBRARIES mnemos::apm::abi mnemos::manifests::genesis mnemos::runtime
    )
endif()
