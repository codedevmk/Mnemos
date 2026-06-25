#include "player_audio.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("player audio uses the adapter-declared source rate for empty startup chunks") {
    mnemos::frontend_sdk::audio_chunk chunk{};
    chunk.samples = nullptr;
    chunk.frame_count = 0U;
    chunk.sample_rate = 24038U;

    CHECK(mnemos::apps::player::select_audio_stream_sample_rate(chunk) == 24038U);
}

TEST_CASE("player audio falls back only when an adapter cannot declare a source rate") {
    mnemos::frontend_sdk::audio_chunk chunk{};

    CHECK(mnemos::apps::player::select_audio_stream_sample_rate(chunk) ==
          mnemos::apps::player::fallback_audio_sample_rate);
}
