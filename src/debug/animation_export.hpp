#pragma once

// System-agnostic animation capture over an assembled player_system. GIF output
// is a compact preview format; movie output is a PNG frame sequence plus JSON
// manifest so actual video codecs can live in an external plugin/encoder.

#include "player_system.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::debug {

    struct animation_frame final {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint32_t> pixels; // 0x00RRGGBB, row-major, packed
    };

    struct movie_sequence_result final {
        std::size_t frames_written{};
        std::string manifest_path;
    };

    // Packs the player's current framebuffer into a tight RGB frame. Returns
    // nullopt when the system has not produced a valid frame yet.
    [[nodiscard]] std::optional<animation_frame>
    capture_animation_frame(const frontend_sdk::player_system& sys);

    // Writes frames as one animated GIF. All frames must share the first frame's
    // dimensions. The GIF delay is derived from `fps_x1000` and clamped to the
    // GIF centisecond granularity.
    [[nodiscard]] bool write_gif_animation(const std::string& path,
                                           std::span<const animation_frame> frames,
                                           std::uint32_t fps_x1000);

    // Writes `<base>.frame000000.png`, `<base>.frame000001.png`, ... and
    // `<base>.movie.json`. The manifest is the plugin seam for MP4/WebM/etc.
    [[nodiscard]] movie_sequence_result
    write_movie_frame_sequence(const std::string& base_path,
                               std::span<const animation_frame> frames,
                               std::uint32_t fps_x1000);

} // namespace mnemos::debug
