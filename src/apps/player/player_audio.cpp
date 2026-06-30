#include "player_audio.hpp"

namespace mnemos::apps::player {

    std::uint32_t select_audio_stream_sample_rate(frontend_sdk::audio_chunk chunk) noexcept {
        return chunk.sample_rate != 0U ? chunk.sample_rate : fallback_audio_sample_rate;
    }

} // namespace mnemos::apps::player
