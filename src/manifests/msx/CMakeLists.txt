add_library(mnemos_manifests_msx msx_system.cpp)
add_library(mnemos::manifests::msx ALIAS mnemos_manifests_msx)

target_include_directories(
  mnemos_manifests_msx
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
         "$<INSTALL_INTERFACE:include>")

target_link_libraries(
  mnemos_manifests_msx
  PUBLIC mnemos::topology
         mnemos::chips::cpu::z80
         mnemos::chips::video::tms9918a
         mnemos::chips::video::v9938
         mnemos::chips::audio::scc
         mnemos::chips::audio::ssg
         mnemos::chips::audio::ym2413
         mnemos::chips::peripheral::rp5c01
         mnemos::chips::storage::msx_cassette
         mnemos::chips::storage::wd1793
         mnemos::chips::mapper::korean_msx_mapper
         mnemos::peripheral::common)

mnemos_apply_common_target_options(mnemos_manifests_msx PUBLIC)
mnemos_declare_tier(
  NAME
  mnemos_manifests_msx
  TIER
  4
  DEPENDS
  mnemos_topology
  mnemos_chips_shared
  mnemos_chips_video_tms9918a
  mnemos_chips_video_v9938
  mnemos_chips_audio_scc
  mnemos_chips_audio_ssg
  mnemos_chips_audio_ym2413
  mnemos_chips_peripheral_rp5c01
  mnemos_chips_storage_msx_cassette
  mnemos_chips_storage_wd1793
  mnemos_chips_mapper_korean_msx_mapper
  mnemos_peripheral_common
  mnemos_foundation)

if(BUILD_TESTING)
  mnemos_add_test(
    mnemos_manifests_msx_test
    SOURCES tests/msx_system_test.cpp
    LIBRARIES mnemos::manifests::msx)
endif()
