#include "cli_args.hpp"

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace mnemos::apps::player::adapters {

    std::optional<std::string> parse_mapper_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string_view{argv[i]} == "--mapper") {
                std::string value{argv[i + 1]};
                for (char& c : value) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (!value.empty()) {
                    return value;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> parse_rom_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            const std::string_view a{argv[i]};
            if (a == "--rom" || a == "-r") {
                return std::string{argv[i + 1]};
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> parse_rom_args(int argc, char* argv[]) {
        std::vector<std::string> paths;
        for (int i = 1; i < argc - 1; ++i) {
            const std::string_view a{argv[i]};
            if (a == "--rom" || a == "-r" || a == "--disk") {
                paths.emplace_back(argv[i + 1]);
                ++i; // skip the consumed value
            }
        }
        return paths;
    }

    bool parse_no_autostart(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--no-autostart") {
                return true;
            }
        }
        return false;
    }

    std::optional<screenshot_request> parse_screenshot_args(int argc, char* argv[]) {
        std::optional<std::string> path;
        std::optional<std::uint64_t> frames;
        for (int i = 1; i < argc - 1; ++i) {
            const std::string_view a{argv[i]};
            if (a == "--screenshot") {
                path = std::string{argv[i + 1]};
            } else if (a == "--frames") {
                frames = std::strtoull(argv[i + 1], nullptr, 10);
            }
        }
        if (path && frames) {
            return screenshot_request{*path, *frames};
        }
        return std::nullopt;
    }

} // namespace mnemos::apps::player::adapters
