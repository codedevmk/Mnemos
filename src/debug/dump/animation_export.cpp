#include "animation_export.hpp"

#include "file.hpp"
#include "gif_image.hpp"
#include "json_util.hpp"
#include "path_id.hpp"
#include "png_image.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>
#include <utility>

namespace mnemos::debug {

    namespace {

        [[nodiscard]] std::uint16_t frame_delay_centiseconds(std::uint32_t fps_x1000) {
            if (fps_x1000 == 0U) {
                return 2U; // 50 Hz fallback, matching PAL's nearest GIF cadence
            }
            std::uint32_t delay = (100000U + (fps_x1000 / 2U)) / fps_x1000;
            delay = std::clamp<std::uint32_t>(delay, 1U, 0xFFFFU);
            return static_cast<std::uint16_t>(delay);
        }

        [[nodiscard]] bool frame_matches(const animation_frame& f, std::uint32_t width,
                                         std::uint32_t height) {
            return width > 0U && height > 0U && f.width == width && f.height == height &&
                   f.pixels.size() == static_cast<std::size_t>(width) * height;
        }

        [[nodiscard]] std::string frame_path(const std::string& base_path, std::size_t index) {
            char suffix[32]{};
            std::snprintf(suffix, sizeof(suffix), ".frame%06zu.png", index);
            return base_path + suffix;
        }

    } // namespace

    std::optional<animation_frame> capture_animation_frame(const frontend_sdk::player_system& sys) {
        const chips::frame_buffer_view fb = sys.current_frame();
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return std::nullopt;
        }
        const std::uint32_t stride = fb.effective_stride();
        animation_frame frame;
        frame.width = fb.width;
        frame.height = fb.height;
        frame.pixels.reserve(static_cast<std::size_t>(fb.width) * fb.height);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            frame.pixels.insert(frame.pixels.end(), row, row + fb.width);
        }
        return frame;
    }

    bool write_gif_animation(const std::string& path, std::span<const animation_frame> frames,
                             std::uint32_t fps_x1000) {
        if (frames.empty() || frames.front().width == 0U || frames.front().height == 0U) {
            return false;
        }
        const std::uint32_t width = frames.front().width;
        const std::uint32_t height = frames.front().height;
        const std::uint16_t delay = frame_delay_centiseconds(fps_x1000);

        std::vector<graphics::images::gif_frame> gif_frames;
        gif_frames.reserve(frames.size());
        for (const animation_frame& frame : frames) {
            if (!frame_matches(frame, width, height)) {
                return false;
            }
            gif_frames.push_back({frame.pixels, delay});
        }

        const graphics::images::gif_animation gif(width, height, std::move(gif_frames));
        if (!gif.write(path)) {
            std::fprintf(stderr, "[animation_export] could not write %s\n", path.c_str());
            return false;
        }
        return true;
    }

    movie_sequence_result write_movie_frame_sequence(const std::string& base_path,
                                                     std::span<const animation_frame> frames,
                                                     std::uint32_t fps_x1000) {
        movie_sequence_result result{.frames_written = 0U,
                                     .manifest_path = base_path + ".movie.json"};
        std::string json =
            "{\n  \"format\": \"png_sequence\",\n  \"fps_x1000\": " + std::to_string(fps_x1000) +
            ",\n  \"frames\": [";

        bool first = true;
        const std::uint32_t width = frames.empty() ? 0U : frames.front().width;
        const std::uint32_t height = frames.empty() ? 0U : frames.front().height;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const animation_frame& frame = frames[i];
            if (!frame_matches(frame, width, height)) {
                std::fprintf(stderr, "[animation_export] skipped frame %zu with mismatched size\n",
                             i);
                continue;
            }
            const std::string path = frame_path(base_path, i);
            const graphics::images::png_image png(frame.width, frame.height, frame.pixels);
            if (!png.write(path)) {
                std::fprintf(stderr, "[animation_export] could not write %s\n", path.c_str());
                continue;
            }

            json += first ? "\n" : ",\n";
            first = false;
            json += "    {\"index\": " + std::to_string(i) +
                    ", \"file\": " + json_string(path_basename(path)) +
                    ", \"width\": " + std::to_string(frame.width) +
                    ", \"height\": " + std::to_string(frame.height) + "}";
            ++result.frames_written;
        }

        json += first ? "]\n}\n" : "\n  ]\n}\n";
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
        if (!io::write_file(result.manifest_path, bytes)) {
            std::fprintf(stderr, "[animation_export] could not write %s\n",
                         result.manifest_path.c_str());
        }
        return result;
    }

} // namespace mnemos::debug
