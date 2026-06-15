add_library(
    mnemos_chips_audio_paula
    paula.cpp
)
add_library(mnemos::chips::audio::paula ALIAS mnemos_chips_audio_paula)

target_include_directories(
    mnemos_chips_audio_paula
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_paula PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_paula PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_paula TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_paula_test
        SOURCES
            tests/paula_test.cpp
        LIBRARIES mnemos::chips::audio::paula
    )
endif()
