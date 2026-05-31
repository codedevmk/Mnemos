add_library(
    mnemos_graphics_images
    image.cpp
    ppm_image.cpp
)
add_library(mnemos::graphics::images ALIAS mnemos_graphics_images)

target_include_directories(
    mnemos_graphics_images
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
)

target_link_libraries(mnemos_graphics_images PUBLIC mnemos::io)

mnemos_apply_common_target_options(mnemos_graphics_images PUBLIC)
mnemos_declare_tier(NAME mnemos_graphics_images TIER 2 DEPENDS mnemos_io)

if(BUILD_TESTING)
    mnemos_add_test(
        mnemos_graphics_images_test
        SOURCES tests/ppm_image_test.cpp
        LIBRARIES mnemos::graphics::images
    )
endif()
