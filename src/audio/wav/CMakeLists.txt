add_library(
    mnemos_audio_wav
    wav.cpp
)
add_library(mnemos::audio::wav ALIAS mnemos_audio_wav)

target_include_directories(
    mnemos_audio_wav
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# io is public (write_wav writes through io::write_file).
target_link_libraries(mnemos_audio_wav PUBLIC mnemos::io)

mnemos_apply_common_target_options(mnemos_audio_wav PUBLIC)
mnemos_declare_tier(
    NAME mnemos_audio_wav
    TIER 2
    DEPENDS mnemos_io
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_audio_wav_test
        SOURCES tests/wav_test.cpp
        LIBRARIES mnemos::audio::wav
    )
endif()
