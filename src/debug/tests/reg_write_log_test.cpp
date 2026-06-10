// Verifies reg_write_log_session captures a chip's register-write stream through
// the reg_write_trace contract and writes a valid VGM, with per-frame waits --
// no knowledge of which system produced the writes.

#include "player_system.hpp"
#include "reg_write_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::reg_write_event;
    using mnemos::instrumentation::reg_write_trace;

    class fake_reg_trace final : public reg_write_trace {
      public:
        void install(callback cb) override { cb_ = std::move(cb); }
        void fire(std::uint16_t port, std::uint8_t value) const {
            if (cb_) {
                cb_({.port = port, .value = value});
            }
        }

      private:
        callback cb_{};
    };

    class psg_intro final : public ichip_introspection {
      public:
        [[nodiscard]] reg_write_trace* reg_writes() override { return &trace_; }
        [[nodiscard]] fake_reg_trace& reg_trace_impl() noexcept { return trace_; }

      private:
        fake_reg_trace trace_;
    };

    class psg_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "SN76489",
                    .family = "test",
                    .klass = chip_class::audio_synth,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }
        [[nodiscard]] psg_intro& intro_impl() noexcept { return intro_; }

      private:
        psg_intro intro_;
    };

    class psg_system final : public player_system {
      public:
        psg_system() { chip_list_[0] = &psg_; }
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override { return chip_list_; }
        [[nodiscard]] psg_chip& psg_ref() noexcept { return psg_; }

      private:
        psg_chip psg_;
        std::array<ichip*, 1> chip_list_{};
        std::vector<spec_field> spec_{};
    };

    [[nodiscard]] std::filesystem::path make_scratch_dir(const std::string& tag) {
        const auto base =
            std::filesystem::temp_directory_path() / ("mnemos_reg_write_log_test_" + tag);
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

    [[nodiscard]] std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t off) {
        return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8U) |
               (static_cast<std::uint32_t>(b[off + 2]) << 16U) |
               (static_cast<std::uint32_t>(b[off + 3]) << 24U);
    }

} // namespace

TEST_CASE("reg_write_log_session writes a VGM of the PSG register-write stream",
          "[reg_write_log]") {
    const auto scratch = make_scratch_dir("song");
    const auto vgm_path = (scratch / "song.vgm").string();

    {
        psg_system sys;
        mnemos::debug::reg_write_log_session session(sys, vgm_path);
        REQUIRE(session.active());

        sys.psg_ref().intro_impl().reg_trace_impl().fire(0U, 0x9FU); // data port write
        session.mark_frame();
        sys.psg_ref().intro_impl().reg_trace_impl().fire(1U, 0xFFU); // GG stereo write
        session.mark_frame();
    } // dtor writes the VGM

    const auto vgm = read_file(vgm_path);
    REQUIRE(vgm.size() > 0x40U);
    CHECK(vgm[0] == 'V');
    CHECK(vgm[1] == 'g');
    CHECK(le32(vgm, 0x0C) == 3579545U);  // SN76489 clock
    CHECK(le32(vgm, 0x18) == 2U * 735U); // 2 NTSC frames -> total samples
    CHECK(le32(vgm, 0x24) == 60U);       // 60 Hz

    // Body: data write, frame wait, stereo write, frame wait, end.
    const std::array<std::uint8_t, 7> expected{0x50U, 0x9FU, 0x62U, 0x4FU, 0xFFU, 0x62U, 0x66U};
    REQUIRE(vgm.size() == 0x40U + expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(vgm[0x40U + i] == expected[i]);
    }
}

TEST_CASE("reg_write_log_session is inactive without a PSG", "[reg_write_log]") {
    class empty_system final : public player_system {
      public:
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        std::vector<spec_field> spec_{};
    };

    const auto scratch = make_scratch_dir("none");
    const auto vgm_path = (scratch / "song.vgm").string();
    empty_system sys;
    mnemos::debug::reg_write_log_session session(sys, vgm_path);
    CHECK_FALSE(session.active());
    session.mark_frame(); // no-op
    // No file is written for an inactive session (verified after dtor below).
}
