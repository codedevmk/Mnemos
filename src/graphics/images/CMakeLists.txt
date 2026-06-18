add_library(
    mnemos_graphics_images
    gif_image.cpp
    image.cpp
    ppm_image.cpp
    png_image.cpp
)
add_library(mnemos::graphics::images ALIAS mnemos_graphics_images)

target_include_directories(
    mnemos_graphics_images
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

# io is public (image::write); compression + crc are png_image.cpp details.
target_link_libraries(
    mnemos_graphics_images
    PUBLIC
        mnemos::io
    PRIVATE
        mnemos::compression
        mnemos::security::cryptography
)

mnemos_apply_common_target_options(mnemos_graphics_images PUBLIC)
mnemos_declare_tier(
    NAME mnemos_graphics_images
    TIER 2
    DEPENDS mnemos_io mnemos_compression mnemos_security_cryptography
)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_graphics_images_test
        SOURCES
            tests/gif_image_test.cpp
            tests/ppm_image_test.cpp
            tests/png_image_test.cpp
        LIBRARIES
            mnemos::graphics::images
            mnemos::compression
            mnemos::security::cryptography
    )
endif()
