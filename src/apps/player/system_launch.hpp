#pragma once

#include "player_system.hpp"
#include "region_args.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player {

    struct system_launch_options final {
        std::vector<std::string> rom_paths{};
        std::optional<std::string> system_arg{};
        bool autostart{true};
        adapters::region_override region_override{adapters::region_override::auto_detect};
        std::optional<std::string> mapper_override{};
        std::optional<std::string> mapper2_override{};
        bool fm_unit{};
        bool light_gun{};
        bool four_score{};
        bool rtc{};
        bool msx2{};
        std::optional<std::uint16_t> dip_override{};
    };

    struct system_launch_outcome final {
        std::unique_ptr<frontend_sdk::player_system> system{};
        std::string primary_media_path{};
        std::string battery_media_path{};
        int exit_code{};
    };

    [[nodiscard]] system_launch_outcome launch_system(const system_launch_options& options);

} // namespace mnemos::apps::player
