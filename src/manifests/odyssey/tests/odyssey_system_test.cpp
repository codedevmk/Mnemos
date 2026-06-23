#include "odyssey_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {
    using mnemos::manifests::odyssey::assemble_odyssey;
    using mnemos::manifests::odyssey::odyssey_card;
    using mnemos::manifests::odyssey::odyssey_config;
    using mnemos::manifests::odyssey::odyssey_controller;

    [[nodiscard]] std::size_t lit_pixels(std::span<const std::uint8_t> luma) {
        return static_cast<std::size_t>(std::ranges::count(luma, static_cast<std::uint8_t>(255U)));
    }
} // namespace

TEST_CASE("assemble_odyssey exposes a discrete no CPU/no ROM/no RAM machine",
          "[odyssey][architecture]") {
    auto system = assemble_odyssey();
    const auto state = system.observable_state();

    CHECK_FALSE(state.cpu_present);
    CHECK_FALSE(state.rom_present);
    CHECK_FALSE(state.ram_present);
    CHECK_FALSE(state.sound_present);
    CHECK(state.card == odyssey_card::table_tennis);
}

TEST_CASE("Odyssey renders only monochrome generated objects", "[odyssey][video]") {
    auto system = assemble_odyssey();
    const auto& frame = system.render_frame();

    CHECK(frame.width == 320U);
    CHECK(frame.height == 240U);
    CHECK(lit_pixels(frame.luma) > 0U);
}

TEST_CASE("Odyssey controller knobs drive the two player spots", "[odyssey][input]") {
    auto system = assemble_odyssey();
    system.set_controller(0U, odyssey_controller{.horizontal = -1.0F, .vertical = 1.0F});
    system.set_controller(1U, odyssey_controller{.horizontal = 1.0F, .vertical = -1.0F});
    system.tick_frame();

    const auto state = system.observable_state();
    CHECK(state.player0.x < state.player1.x);
    CHECK(state.player0.y < state.player1.y);
    CHECK(state.frame == 1U);
}

TEST_CASE("Odyssey cards rewire visible generated elements", "[odyssey][cards]") {
    auto system = assemble_odyssey({.card = odyssey_card::ski});
    auto state = system.observable_state();
    CHECK(state.card == odyssey_card::ski);
    CHECK_FALSE(state.line_visible);
    CHECK_FALSE(state.ball.visible);

    system.set_card(odyssey_card::tennis);
    state = system.observable_state();
    CHECK(state.card == odyssey_card::tennis);
    CHECK(state.line_visible);
    CHECK(state.ball.visible);
}

TEST_CASE("Odyssey reset button recenters the generated ball", "[odyssey][input]") {
    auto system = assemble_odyssey();
    system.tick_frame();
    const auto moved = system.observable_state().ball.x;

    system.set_controller(0U, odyssey_controller{.reset = true});
    system.tick_frame();
    const auto reset = system.observable_state().ball.x;

    CHECK(moved != reset);
    CHECK(reset > 150);
    CHECK(reset < 170);
}

TEST_CASE("Odyssey frame dimensions clamp to the fixed internal framebuffer", "[odyssey][video]") {
    auto system = assemble_odyssey(odyssey_config{.width = 640U, .height = 480U});
    const auto& frame = system.render_frame();

    CHECK(frame.width == 320U);
    CHECK(frame.height == 240U);
}
