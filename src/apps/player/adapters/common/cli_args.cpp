#include "cli_args.hpp"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters {

    namespace {
        std::optional<std::string> parse_lowercase_value_arg(int argc, char* argv[],
                                                             std::string_view flag) {
            for (int i = 1; i < argc - 1; ++i) {
                if (std::string_view{argv[i]} == flag) {
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
    } // namespace

    std::optional<std::string> parse_mapper_arg(int argc, char* argv[]) {
        return parse_lowercase_value_arg(argc, argv, "--mapper");
    }

    std::optional<std::string> parse_mapper2_arg(int argc, char* argv[]) {
        return parse_lowercase_value_arg(argc, argv, "--mapper2");
    }

    std::optional<std::uint16_t> parse_dip_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string_view{argv[i]} == "--dip") {
                char* end = nullptr;
                const unsigned long value = std::strtoul(argv[i + 1], &end, 0);
                if (end != argv[i + 1] && *end == '\0' && value <= 0xFFFFUL) {
                    return static_cast<std::uint16_t>(value);
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool parse_fm_unit_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--fm") {
                return true;
            }
        }
        return false;
    }

    bool parse_light_gun_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--light-gun") {
                return true;
            }
        }
        return false;
    }

    bool parse_four_score_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--four-score") {
                return true;
            }
        }
        return false;
    }

    bool parse_rtc_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--rtc") {
                return true;
            }
        }
        return false;
    }

    bool parse_msx2_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--msx2") {
                return true;
            }
        }
        return false;
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

    std::optional<std::string> parse_system_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            const std::string_view a{argv[i]};
            if (a == "--system" || a == "-s") {
                std::string value{argv[i + 1]};
                if (!value.empty()) {
                    return value;
                }
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

    bool parse_capabilities_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view{argv[i]} == "--capabilities") {
                return true;
            }
        }
        return false;
    }

    namespace {
        // Set the named button on `s`; returns false for an unknown name.
        bool apply_button(mnemos::peripheral::controller_state& s, std::string_view b) {
            if (b == "up") {
                s.up = true;
            } else if (b == "down") {
                s.down = true;
            } else if (b == "left") {
                s.left = true;
            } else if (b == "right") {
                s.right = true;
            } else if (b == "start") {
                s.start = true;
            } else if (b == "select") {
                s.select = true;
            } else if (b == "a") {
                s.a = true;
            } else if (b == "b") {
                s.b = true;
            } else if (b == "c") {
                s.c = true;
            } else if (b == "x") {
                s.x = true;
            } else if (b == "y") {
                s.y = true;
            } else if (b == "z") {
                s.z = true;
            } else if (b == "mode") {
                s.mode = true;
            } else {
                return false;
            }
            return true;
        }

        // Parse one `<button>@<frame>[+duration]` spec.
        std::optional<press_event> parse_press_spec(std::string_view spec) {
            const auto at = spec.find('@');
            if (at == std::string_view::npos || at == 0U) {
                return std::nullopt;
            }
            std::string button{spec.substr(0, at)};
            for (char& c : button) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            mnemos::peripheral::controller_state probe{};
            if (!apply_button(probe, button)) {
                return std::nullopt; // unknown button name
            }
            std::string_view rest = spec.substr(at + 1U);
            const auto plus = rest.find('+');
            const std::string frame_str{rest.substr(0, plus)};
            if (frame_str.empty()) {
                return std::nullopt;
            }
            press_event e;
            e.button = std::move(button);
            e.frame = std::strtoull(frame_str.c_str(), nullptr, 10);
            if (plus != std::string_view::npos) {
                const std::string dur_str{rest.substr(plus + 1U)};
                const std::uint64_t d = std::strtoull(dur_str.c_str(), nullptr, 10);
                e.duration = d > 0U ? d : press_default_duration;
            }
            return e;
        }
    } // namespace

    std::vector<press_event> parse_press_events(int argc, char* argv[]) {
        std::vector<press_event> events;
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string_view{argv[i]} == "--press") {
                if (auto e = parse_press_spec(argv[i + 1])) {
                    events.push_back(std::move(*e));
                }
                ++i; // skip the consumed value
            }
        }
        return events;
    }

    mnemos::peripheral::controller_state input_for_frame(const std::vector<press_event>& events,
                                                         std::uint64_t frame) noexcept {
        mnemos::peripheral::controller_state state{};
        for (const press_event& e : events) {
            if (frame >= e.frame && frame < e.frame + e.duration) {
                apply_button(state, e.button);
            }
        }
        return state;
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

    namespace {
        // Shared parse for the `--extract-* <base> [--extract-frames N]` family:
        // returns the base path (rejecting an option-shaped value) and the frame
        // count (default 0), or nullopt when `base_flag` is absent/valueless.
        std::optional<std::pair<std::string, std::uint64_t>>
        parse_base_and_frames(int argc, char* argv[], std::string_view base_flag) {
            std::optional<std::string> base;
            std::uint64_t frames = 0U;
            for (int i = 1; i < argc - 1; ++i) {
                const std::string_view a{argv[i]};
                if (a == base_flag) {
                    const std::string_view value{argv[i + 1]};
                    // Reject an option-shaped token (e.g. a stray "--extract-frames"
                    // with no base path); leaving base unset disables the path.
                    if (!value.empty() && !value.starts_with("--")) {
                        base = std::string{value};
                        ++i; // skip the consumed value
                    }
                } else if (a == "--extract-frames") {
                    frames = std::strtoull(argv[i + 1], nullptr, 10);
                    ++i; // skip the consumed value
                }
            }
            if (base) {
                return std::pair{*base, frames};
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::uint64_t> parse_positive_frame_count(const char* value) {
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(value, &end, 10);
            if (end == value || *end != '\0' || parsed == 0ULL) {
                return std::nullopt;
            }
            return static_cast<std::uint64_t>(parsed);
        }
    } // namespace

    std::optional<extract_assets_request> parse_extract_assets_args(int argc, char* argv[]) {
        if (auto r = parse_base_and_frames(argc, argv, "--extract-assets")) {
            return extract_assets_request{r->first, r->second};
        }
        return std::nullopt;
    }

    std::optional<extract_audio_request> parse_extract_audio_args(int argc, char* argv[]) {
        if (auto r = parse_base_and_frames(argc, argv, "--extract-audio")) {
            return extract_audio_request{r->first, r->second};
        }
        return std::nullopt;
    }

    std::optional<animation_record_request> parse_animation_record_args(int argc, char* argv[]) {
        std::optional<std::string> output;
        std::optional<animation_record_format> format;
        std::optional<std::uint64_t> frames;
        for (int i = 1; i < argc; ++i) {
            const std::string_view a{argv[i]};
            if ((a == "--record-gif" || a == "--record-movie") && i < argc - 1) {
                const std::string_view value{argv[i + 1]};
                if (!value.empty() && !value.starts_with("--")) {
                    output = std::string{value};
                    format = a == "--record-gif" ? animation_record_format::gif
                                                 : animation_record_format::movie_frames;
                    ++i; // skip the consumed value
                }
            } else if (a == "--frames" && i < argc - 1) {
                frames = parse_positive_frame_count(argv[i + 1]);
                ++i; // skip the consumed value
            }
        }
        if (output && format && frames) {
            return animation_record_request{*output, *frames, *format};
        }
        return std::nullopt;
    }

} // namespace mnemos::apps::player::adapters
