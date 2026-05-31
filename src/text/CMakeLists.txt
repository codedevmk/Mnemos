add_library(
    mnemos_text
    encoding.cpp
    decoding.cpp
)
add_library(mnemos::text ALIAS mnemos_text)

target_include_directories(
    mnemos_text
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_text PUBLIC)
mnemos_declare_tier(NAME mnemos_text TIER 1)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_text_test
        SOURCES
            tests/encoding_test.cpp
            tests/decoding_test.cpp
        LIBRARIES mnemos::text
    )
endif()
