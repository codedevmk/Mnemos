add_library(
    mnemos_compression
    deflate.cpp
    inflate.cpp
    lzma1.cpp
    zip_archive.cpp
)
add_library(mnemos::compression ALIAS mnemos_compression)

target_include_directories(
    mnemos_compression
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

mnemos_apply_common_target_options(mnemos_compression PUBLIC)
mnemos_declare_tier(NAME mnemos_compression TIER 2)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_compression_test
        SOURCES
            tests/deflate_test.cpp
            tests/inflate_test.cpp
            tests/lzma1_test.cpp
            tests/zip_archive_test.cpp
        LIBRARIES mnemos::compression
    )
endif()
