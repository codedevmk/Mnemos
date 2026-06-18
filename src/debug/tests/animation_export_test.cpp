// Verifies animation export stays system-agnostic: it packs player_system's
// current framebuffer, then writes either GIF or a PNG sequence + manifest.

#include "animation_export.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::frame_buffer_view;
    using mnemos::debug::animation_frame;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;

    class frame_system final : public player_system {
      public:
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override {
            return {.pixels = pixels_.data(), .width = 2U, .height = 2U, .stride = 3U};
        }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        // Two visible pixels per row plus one stride-padding pixel to ignore.
        std::array<std::uint32_t, 6> pixels_{0x010203U, 0x040506U, 0xDEAD00U,
                                            0x070809U, 0x0A0B0CU, 0xBEEF00U};
        std::vector<spec_field> spec_{};
    };

    [[nodiscard]] std::filesystem::path make_scratch_dir(const std::string& tag) {
        const auto base =
            std::filesystem::temp_directory_path() / ("mnemos_animation_export_test_" + tag);
        std::filesystem::remove_all(base);
        std::filesystem::create_directories(base);
        return base;
    }

    [[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::vector<std::uint8_t> out;
        if (!in) {
            return out;
        }
        in.seekg(0, std::ios::end);
        out.resize(static_cast<std::size_t>(in.tellg()));
        in.seekg(0, std::ios::beg);
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return out;
    }

    [[nodiscard]] std::string read_text(const std::filesystem::path& p) {
        const auto bytes = read_file(p);
        return std::string(bytes.begin(), bytes.end());
    }

    [[nodiscard]] bool is_png(const std::vector<std::uint8_t>& b) {
        return b.size() >= 8U && b[0] == 0x89U && b[1] == 0x50U && b[2] == 0x4EU &&
               b[3] == 0x47U;
    }

    [[nodiscard]] bool is_gif(const std::vector<std::uint8_t>& b) {
        return b.size() >= 6U && b[0] == static_cast<std::uint8_t>('G') &&
               b[1] == static_cast<std::uint8_t>('I') &&
               b[2] == static_cast<std::uint8_t>('F') &&
               b[3] == static_cast<std::uint8_t>('8') &&
               b[4] == static_cast<std::uint8_t>('9') &&
               b[5] == static_cast<std::uint8_t>('a');
    }

} // namespace

TEST_CASE("capture_animation_frame packs a strided framebuffer", "[animation_export]") {
    frame_system sys;
    const auto frame = mnemos::debug::capture_animation_frame(sys);
    REQUIRE(frame.has_value());
    CHECK(frame->width == 2U);
    CHECK(frame->height == 2U);
    CHECK(frame->pixels == std::vector<std::uint32_t>{0x010203U, 0x040506U, 0x070809U,
                                                       0x0A0B0CU});
}

TEST_CASE("write_gif_animation writes an animated GIF", "[animation_export]") {
    const auto scratch = make_scratch_dir("gif");
    const auto path = scratch / "clip.gif";
    const std::vector<animation_frame> frames = {
        {2U, 1U, {0xFF0000U, 0x00FF00U}},
        {2U, 1U, {0x0000FFU, 0xFFFFFFU}},
    };

    REQUIRE(mnemos::debug::write_gif_animation(path.string(), frames, 60000U));
    CHECK(is_gif(read_file(path)));
}

TEST_CASE("write_movie_frame_sequence writes PNG frames and manifest", "[animation_export]") {
    const auto scratch = make_scratch_dir("movie");
    const auto base = (scratch / "clip").string();
    const std::vector<animation_frame> frames = {
        {1U, 1U, {0x112233U}},
        {1U, 1U, {0x445566U}},
    };

    const auto result = mnemos::debug::write_movie_frame_sequence(base, frames, 50000U);
    CHECK(result.frames_written == 2U);
    CHECK(result.manifest_path == base + ".movie.json");

    const auto frame0 = scratch / "clip.frame000000.png";
    const auto frame1 = scratch / "clip.frame000001.png";
    REQUIRE(std::filesystem::exists(frame0));
    REQUIRE(std::filesystem::exists(frame1));
    CHECK(is_png(read_file(frame0)));
    CHECK(is_png(read_file(frame1)));

    const std::string json = read_text(scratch / "clip.movie.json");
    CHECK(json.find("\"format\": \"png_sequence\"") != std::string::npos);
    CHECK(json.find("\"fps_x1000\": 50000") != std::string::npos);
    CHECK(json.find("\"file\": \"clip.frame000000.png\"") != std::string::npos);
    CHECK(json.find("\"file\": \"clip.frame000001.png\"") != std::string::npos);
}
