# The tracer host (mnemos_tracer.exe): the APM "agent". Loads an emulator plugin
# DLL at runtime and observes it from the outside via page-protection watchpoints.
# Link-depends ONLY on the pure-C ABI and the watchpoint engine -- never the engine.
add_executable(mnemos_tracer tracer_main.cpp plugin_host.cpp)

target_link_libraries(
    mnemos_tracer
    PRIVATE
        mnemos::apm::abi
        mnemos::apm::memory
)

mnemos_apply_common_target_options(mnemos_tracer PRIVATE)
