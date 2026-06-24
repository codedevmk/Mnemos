#pragma once

#include "cli_args.hpp"

#include <optional>

namespace mnemos::frontend_sdk {
    class player_system;
}

namespace mnemos::apps::player {

    struct headless_requests final {
        std::optional<adapters::screenshot_request> screenshot{};
        std::optional<adapters::save_state_request> save_state{};
        std::optional<std::string> load_state{};
        std::optional<adapters::extract_assets_request> extract_assets{};
        std::optional<adapters::extract_audio_request> extract_audio{};
        std::optional<adapters::animation_record_request> record_animation{};
        bool capabilities{};
    };

    [[nodiscard]] bool has_headless_request(const headless_requests& requests) noexcept;

    [[nodiscard]] std::optional<int> run_headless_request(frontend_sdk::player_system* system,
                                                          const headless_requests& requests,
                                                          int argc, char* argv[]);

} // namespace mnemos::apps::player
