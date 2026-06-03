# Tagged-allocator + page-protection watchpoint engine for the APM tracer. The
# heart of "observe from the outside": VirtualProtect + a Vectored Exception
# Handler intercept guest-memory accesses without the traced program's help.
add_library(mnemos_apm_memory page_guard.cpp bank_registry.cpp tagged_allocator.cpp)
add_library(mnemos::apm::memory ALIAS mnemos_apm_memory)

target_include_directories(
    mnemos_apm_memory
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_apm_memory PUBLIC)

# Note: APM targets sit outside the engine tier DAG (mnemos_declare_tier) -- the
# tracer is a separate capability that loads the engine as a runtime plugin.

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_apm_memory_page_guard_test
        SOURCES tests/page_guard_proof.cpp
        LIBRARIES mnemos::apm::memory
    )
    mnemos_add_test(
        mnemos_apm_memory_bank_registry_test
        SOURCES tests/bank_registry_test.cpp
        LIBRARIES mnemos::apm::memory
    )
    mnemos_add_test(
        mnemos_apm_memory_tagged_allocator_test
        SOURCES tests/tagged_allocator_test.cpp
        LIBRARIES mnemos::apm::memory
    )
endif()
