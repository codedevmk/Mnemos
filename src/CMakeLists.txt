add_subdirectory(foundation)
add_subdirectory(extensions)
add_subdirectory(common)
add_subdirectory(compression)
add_subdirectory(disc)
add_subdirectory(security)
add_subdirectory(text)
add_subdirectory(io)
add_subdirectory(dsp)
add_subdirectory(graphics)
add_subdirectory(audio)
add_subdirectory(chips)
add_subdirectory(peripheral_sdk)
add_subdirectory(topology)
add_subdirectory(manifests)
add_subdirectory(runtime)
add_subdirectory(instrumentation)
add_subdirectory(frontend_sdk)
add_subdirectory(debug)

# Frontend applications pull in SDL3 (and its windowing/audio system
# dependencies). Headless builds (e.g. the ARM64 core-correctness CI job, or a
# future NDK build that supplies its own frontend) skip them via
# -DMNEMOS_BUILD_APPS=OFF; the deterministic core and tests do not depend on
# anything under apps/.
if(MNEMOS_BUILD_APPS)
    add_subdirectory(apps)
endif()
