#pragma once

#include "player_system.hpp"

#include <cstdint>

namespace mnemos::apps::player {

    inline constexpr std::uint32_t fallback_audio_sample_rate = 48000U;

    [[nodiscard]] std::uint32_t
    select_audio_stream_sample_rate(frontend_sdk::audio_chunk chunk) noexcept;

} // namespace mnemos::apps::player
