// Verifies the system-agnostic debug-dump helpers walk player_system through
// the introspection surface only -- no downcasts, no system-specific paths.

#include "debug_dump.hpp"
#include "introspection_views.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
    using mnemos::instrumentation::debug_layer;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::memory_view;
    using mnemos::instrumentation::trace_event;
    using mnemos::instrumentation::trace_target;

    class bytes_view final : public memory_view {
      public:
        bytes_view(std::string_view name, std::span<const std::uint8_t> bytes) noexcept
            : name_(name), bytes_(bytes) {}
        [[nodiscard]] std::string_view name() const noexcept override { return name_; }
        [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept override {
            return bytes_;
        }

      private:
        std::string_view name_;
        std::span<const std::uint8_t> bytes_;
    };

    class fake_trace final : public trace_target {
      public:
        void install(callback cb) override { last_ = std::move(cb); }
        void fire(trace_event ev) {
            if (last_) {
                last_(ev);
            }
        }

      private:
        callback last_{};
    };

    class trace_intro final : public ichip_introspection {
      public:
        trace_intro() = default;
        [[nodiscard]] trace_target* trace() override { return &trace_; }
        [[nodiscard]] fake_trace& trace_impl() noexcept { return trace_; }

      private:
        fake_trace trace_;
    };

    class memory_intro final : public ichip_introspection {
      public:
        memory_intro(std::span<const std::uint8_t> ram)
            : ram_("ram", ram) {
            table_[0] = &ram_;
        }
        [[nodiscard]] std::span<memory_view* const> memory_views() override { return table_; }

      private:
        bytes_view ram_;
        std::array<memory_view*, 1> table_{};
    };

    class trace_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "cpu_x",
                    .family = "test",
                    .klass = chip_class::cpu,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }
        [[nodiscard]] trace_intro& intro_impl() noexcept { return intro_; }

      private:
        trace_intro intro_;
    };

    class memory_chip final : public ichip {
      public:
        explicit memory_chip(std::span<const std::uint8_t> ram) : intro_(ram) {}

        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "mem.chip-1",
                    .family = "test",
                    .klass = chip_class::peripheral,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        memory_intro intro_;
    };

    class fake_system final : public player_system {
      public:
        fake_system() {
            // 8 pixels, 0x00RRGGBB packed. The dump should emit 8 RGB triplets.
            framebuffer_ = {0x010203U, 0x040506U, 0x070809U, 0x0A0B0CU,
                            0x0D0E0FU, 0x101112U, 0x131415U, 0x161718U};
            ram_ = {0xDEU, 0xADU, 0xBEU, 0xEFU};
            mem_chip_ = std::make_unique<memory_chip>(ram_);
            trace_chip_ = std::make_unique<trace_chip>();
            chip_list_[0] = trace_chip_.get();
            chip_list_[1] = mem_chip_.get();
        }

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override {
            return {.pixels = framebuffer_.data(), .width = 4U, .height = 2U, .stride = 0U};
        }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override {
            return chip_list_;
        }

        [[nodiscard]] trace_chip& trace_chip_ref() noexcept { return *trace_chip_; }

      private:
        std::vector<std::uint32_t> framebuffer_{};
        std::vector<std::uint8_t> ram_{};
        std::unique_ptr<memory_chip> mem_chip_{};
        std::unique_ptr<trace_chip> trace_chip_{};
        std::array<ichip*, 2> chip_list_{};
        std::vector<spec_field> spec_{};
    };

    [[nodiscard]] std::filesystem::path make_scratch_dir(const std::string& tag) {
        const auto base = std::filesystem::temp_directory_path() /
                          ("mnemos_debug_dump_test_" + tag);
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
        in.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
        return out;
    }

} // namespace

TEST_CASE("dump_screenshot_artifacts writes framebuffer PPM + per-chip sidecars",
          "[debug_dump]") {
    const auto scratch = make_scratch_dir("artifacts");
    const auto base = (scratch / "shot.ppm").string();

    fake_system sys;
    REQUIRE(mnemos::apps::player::dump_screenshot_artifacts(sys, base));

    // Primary framebuffer PPM exists with 8 pixels * 3 bytes + header.
    const auto ppm = read_file(base);
    REQUIRE_FALSE(ppm.empty());
    const std::string header = "P6\n4 2\n255\n";
    REQUIRE(ppm.size() == header.size() + 8U * 3U);

    // The memory_chip's sole memory_view "ram" is dumped under the
    // chip's sanitized part_number ("mem.chip-1" -> "mem_chip_1").
    const auto mem_path = scratch / "shot.ppm.mem_chip_1.ram.bin";
    REQUIRE(std::filesystem::exists(mem_path));
    const auto mem_bytes = read_file(mem_path);
    REQUIRE(mem_bytes == std::vector<std::uint8_t>{0xDEU, 0xADU, 0xBEU, 0xEFU});

    // The trace_chip advertises only a trace_target, no memory_views -- so
    // there should be NO sidecar bin file for it.
    for (auto& entry : std::filesystem::directory_iterator(scratch)) {
        const std::string name = entry.path().filename().string();
        CHECK(name.find("cpu_x") == std::string::npos);
    }
}

TEST_CASE("trace_csv_session installs against the first traceable chip", "[debug_dump]") {
    const auto scratch = make_scratch_dir("trace");
    const auto csv = (scratch / "t.csv").string();

    fake_system sys;
    std::uint64_t frame = 42U;
    {
        mnemos::apps::player::trace_csv_session session(sys, csv, frame);
        REQUIRE(session.active());

        // Fire two synthetic events as if the chip had executed two instructions.
        sys.trace_chip_ref().intro_impl().trace_impl().fire(
            {.pc = 0x1234U, .cycles = 100U});
        sys.trace_chip_ref().intro_impl().trace_impl().fire(
            {.pc = 0x5678U, .cycles = 220U});
    }

    // After the session ends, fire another event -- it must NOT appear in the
    // CSV because the dtor cleared the callback.
    sys.trace_chip_ref().intro_impl().trace_impl().fire(
        {.pc = 0xDEADU, .cycles = 999U});

    std::ifstream in(csv);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    REQUIRE(lines.size() == 3U); // header + 2 rows
    CHECK(lines[0] == "frame,inst,pc,cycles");
    CHECK(lines[1] == "42,0,001234,100");
    CHECK(lines[2] == "42,1,005678,220");
}

TEST_CASE("trace_csv_session is inactive when no chip advertises a trace_target",
          "[debug_dump]") {
    // A player_system that returns an empty chips() span.
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

    empty_system sys;
    std::uint64_t frame = 0U;
    const auto scratch = make_scratch_dir("trace_empty");
    mnemos::apps::player::trace_csv_session session(
        sys, (scratch / "x.csv").string(), frame);
    CHECK_FALSE(session.active());
}
