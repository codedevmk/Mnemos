#include "debug_session.hpp"
#include "introspection_views.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::chips::register_descriptor;
    using mnemos::chips::register_value_format;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::debug::debug_session;
    using mnemos::debug::debug_surface_kind;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;
    using mnemos::instrumentation::debug_layer;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::memory_view;
    using mnemos::instrumentation::register_view;
    using mnemos::instrumentation::span_memory_view;

    class fake_registers final : public register_view {
      public:
        [[nodiscard]] std::span<const register_descriptor> registers() override { return regs_; }

      private:
        std::array<register_descriptor, 2> regs_{
            register_descriptor{.name = "PC",
                                .value = 0x123456U,
                                .bit_width = 24U,
                                .format = register_value_format::unsigned_integer},
            register_descriptor{
                .name = "INTENA", .value = 0x602CU, .bit_width = 16U, .format = register_value_format::flags}};
    };

    class fake_layer final : public debug_layer {
      public:
        [[nodiscard]] std::string_view name() const noexcept override { return "sprites"; }
        [[nodiscard]] frame_buffer_view view() const override {
            return {.pixels = pixels_.data(), .width = 2U, .height = 2U, .stride = 4U};
        }

      private:
        std::array<std::uint32_t, 8> pixels_{
            0x00000001U, 0x00000002U, 0x00000000U, 0x00000000U,
            0x00000003U, 0x00000004U, 0x00000000U, 0x00000000U};
    };

    class fake_intro final : public ichip_introspection {
      public:
        fake_intro() {
            memory_table_[0] = &vram_view_;
            layer_table_[0] = &layer_;
        }

        [[nodiscard]] std::span<memory_view* const> memory_views() override {
            return memory_table_;
        }
        [[nodiscard]] register_view* registers() override { return &registers_; }
        [[nodiscard]] std::span<debug_layer* const> debug_layers() override { return layer_table_; }

      private:
        std::array<std::uint8_t, 3> vram_{0xAAU, 0xBBU, 0xCCU};
        span_memory_view vram_view_{"Video RAM", vram_};
        std::array<memory_view*, 1> memory_table_{};
        fake_registers registers_;
        fake_layer layer_;
        std::array<debug_layer*, 1> layer_table_{};
    };

    class fake_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "Agnus 8367",
                    .family = "video",
                    .klass = chip_class::video,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        fake_intro intro_;
    };

    class fake_system final : public player_system {
      public:
        fake_system() {
            chip_table_[0] = &chip_;
            memory_table_[0] = &work_ram_view_;
        }

        [[nodiscard]] video_region region() const noexcept override { return {50000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override {
            return {.pixels = frame_.data(), .width = 2U, .height = 2U, .stride = 4U};
        }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override {
            return chip_table_;
        }
        [[nodiscard]] std::span<memory_view* const> memory_views() const noexcept override {
            return memory_table_;
        }

      private:
        std::vector<spec_field> spec_{};
        std::array<std::uint32_t, 8> frame_{
            0x00000010U, 0x00000020U, 0x00000000U, 0x00000000U,
            0x00000030U, 0x00000040U, 0x00000000U, 0x00000000U};
        std::array<std::uint8_t, 4> work_ram_{0x11U, 0x22U, 0x33U, 0x44U};
        span_memory_view work_ram_view_{"Work RAM", work_ram_};
        fake_chip chip_;
        std::array<ichip*, 1> chip_table_{};
        std::array<memory_view*, 1> memory_table_{};
    };

    [[nodiscard]] bool has_surface(const std::vector<mnemos::debug::debug_surface_descriptor>& list,
                                   std::string_view id, debug_surface_kind kind) {
        for (const auto& surface : list) {
            if (surface.id == id && surface.kind == kind) {
                return true;
            }
        }
        return false;
    }

} // namespace

TEST_CASE("debug_session enumerates system-agnostic memory registers and layers",
          "[debug][session]") {
    fake_system sys;
    debug_session session(sys);

    const auto surfaces = session.enumerate();

    CHECK(has_surface(surfaces, "video.primary", debug_surface_kind::primary_frame));
    CHECK(has_surface(surfaces, "memory.system.work_ram", debug_surface_kind::memory_space));
    CHECK(has_surface(surfaces, "memory.agnus_8367.video_ram", debug_surface_kind::memory_space));
    CHECK(has_surface(surfaces, "register_bank.agnus_8367", debug_surface_kind::register_bank));
    CHECK(has_surface(surfaces, "debug_layer.agnus_8367.sprites", debug_surface_kind::debug_layer));
}

TEST_CASE("debug_session captures copied snapshots by stable id", "[debug][session]") {
    fake_system sys;
    debug_session session(sys);

    const auto memory = session.capture_memory("memory.system.work_ram");
    REQUIRE(memory.has_value());
    CHECK(memory->bytes == std::vector<std::uint8_t>{0x11U, 0x22U, 0x33U, 0x44U});

    const auto registers = session.capture_registers("register_bank.agnus_8367");
    REQUIRE(registers.has_value());
    REQUIRE(registers->registers.size() == 2U);
    CHECK(registers->registers[0].name == "PC");
    CHECK(registers->registers[0].value == 0x123456U);
    CHECK(registers->registers[1].format == register_value_format::flags);

    const auto primary = session.capture_frame("video.primary");
    REQUIRE(primary.has_value());
    CHECK(primary->width == 2U);
    CHECK(primary->height == 2U);
    CHECK(primary->pixels == std::vector<std::uint32_t>{0x10U, 0x20U, 0x30U, 0x40U});

    const auto sprites = session.capture_frame("debug_layer.agnus_8367.sprites");
    REQUIRE(sprites.has_value());
    CHECK(sprites->pixels == std::vector<std::uint32_t>{0x01U, 0x02U, 0x03U, 0x04U});
    CHECK_FALSE(session.capture_memory("missing.id").has_value());
}
