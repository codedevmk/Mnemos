# The plugin contract: pure-C, header-only, zero dependencies. Shared by the
# tracer host and every engine binding.
add_library(mnemos_apm_abi INTERFACE)
add_library(mnemos::apm::abi ALIAS mnemos_apm_abi)

target_include_directories(
    mnemos_apm_abi
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)
