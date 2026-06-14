add_library(
    mnemos_chips_audio_ssg
    ssg.cpp
)
add_library(mnemos::chips::audio::ssg ALIAS mnemos_chips_audio_ssg)

target_include_directories(
    mnemos_chips_audio_ssg
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_chips_audio_ssg PUBLIC mnemos::chips::shared)

mnemos_apply_common_target_options(mnemos_chips_audio_ssg PUBLIC)
mnemos_declare_tier(NAME mnemos_chips_audio_ssg TIER 2 DEPENDS mnemos_foundation)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_chips_audio_ssg_test
        SOURCES
            tests/ssg_test.cpp
        LIBRARIES mnemos::chips::audio::ssg
    )
endif()
