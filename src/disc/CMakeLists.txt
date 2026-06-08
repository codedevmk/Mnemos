add_library(
    mnemos_disc
    circ_ecc.cpp
    disc_image.cpp
)
add_library(mnemos::disc ALIAS mnemos_disc)

target_include_directories(
    mnemos_disc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_disc PRIVATE mnemos::io)

mnemos_apply_common_target_options(mnemos_disc PUBLIC)
mnemos_declare_tier(NAME mnemos_disc TIER 2)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_disc_test
        SOURCES
            tests/circ_ecc_test.cpp
            tests/disc_image_test.cpp
        LIBRARIES mnemos::disc
    )
endif()
