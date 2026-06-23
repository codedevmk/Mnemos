# Artifact dumping over an assembled player_system: framebuffer -> PPM, each
# chip/system memory_view -> .bin, each debug_layer -> .ppm, CSV traces, decoded
# assets/audio extraction, and register-write logging. Drives systems through
# the frontend_sdk introspection surface only.
add_library(
    mnemos_debug_dump
    animation_export.cpp
    debug_dump.cpp
    capability_discovery.cpp
    game_watch.cpp
    asset_export.cpp
    audio_export.cpp
    reg_write_log.cpp
)
add_library(mnemos::debug::dump ALIAS mnemos_debug_dump)

target_include_directories(
    mnemos_debug_dump
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(
    mnemos_debug_dump
    PUBLIC
        mnemos::frontend_sdk
        mnemos::chips::shared
    PRIVATE
        mnemos::io
        mnemos::graphics::images
        mnemos::audio::wav
        mnemos::audio::vgm
)

mnemos_apply_common_target_options(mnemos_debug_dump PUBLIC)
mnemos_declare_tier(
    NAME mnemos_debug_dump
    TIER 8
    DEPENDS
        mnemos_frontend_sdk
        mnemos_chips_shared
        mnemos_io
        mnemos_graphics_images
        mnemos_audio_wav
        mnemos_audio_vgm
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_debug_dump_test
        SOURCES
            tests/animation_export_test.cpp
            tests/debug_dump_test.cpp
            tests/capability_discovery_test.cpp
            tests/game_watch_test.cpp
            tests/asset_export_test.cpp
            tests/audio_export_test.cpp
            tests/reg_write_log_test.cpp
        LIBRARIES mnemos::debug::dump
    )
endif()
