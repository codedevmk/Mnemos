#include "upd94244.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    [[nodiscard]] bool frame_has_nonblack(const mnemos::chips::frame_buffer_view& frame) {
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                if (frame.pixels[static_cast<std::size_t>(y) * frame.effective_stride() + x] !=
                    0U) {
                    return true;
                }
            }
        }
        return false;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ivideo, mnemos::chips::video::upd94244>);

TEST_CASE("upd94244 reports NEC VDP identity and factory id") {
    const mnemos::chips::video::upd94244 vdp;
    const auto md = vdp.metadata();
    CHECK(md.manufacturer == "NEC");
    CHECK(md.part_number == "uPD94244-210");
    CHECK(md.family == "uPD94244");
    CHECK(md.klass == mnemos::chips::chip_class::video);

    auto chip = mnemos::chips::create_chip("nec.upd94244");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("uPD94244-210"));
}

TEST_CASE("upd94244 exposes registers, VRAM, and nonblank diagnostic frames") {
    mnemos::chips::video::upd94244 vdp;
    std::vector<std::uint8_t> gfx(0x4000U, 0x33U);
    std::vector<std::uint8_t> main(0x1000U, 0x55U);
    std::vector<std::uint8_t> ymz(0x1000U, 0x88U);
    std::vector<std::uint8_t> work(0x800U, 0x22U);
    std::vector<std::uint8_t> nvram(0x100U, 0x11U);
    vdp.attach_vdp_rom(gfx);
    vdp.write_register(3U, 7U);
    vdp.write_vram(7U, 0xA5U);

    vdp.compose_diagnostic(main, ymz, work, nvram, 0xF0U, 0x0FU);
    CHECK(vdp.frame_index() == 1U);
    CHECK(vdp.read_register(3U) == 7U);
    CHECK(vdp.read_vram(7U) == 0xA5U);
    CHECK(frame_has_nonblack(vdp.framebuffer()));
}

TEST_CASE("upd94244 state round-trips registers, VRAM, and framebuffer") {
    mnemos::chips::video::upd94244 source;
    std::vector<std::uint8_t> gfx(0x1000U, 0x3CU);
    source.attach_vdp_rom(gfx);
    source.write_register(1U, 0x12345678U);
    source.write_vram(2U, 0x44U);
    source.compose_diagnostic(gfx, gfx, gfx, gfx, 0x12U, 0x34U);

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source.save_state(writer);

    mnemos::chips::video::upd94244 restored;
    mnemos::chips::state_reader reader(state);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.frame_index() == 1U);
    CHECK(restored.read_register(1U) == 0x12345678U);
    CHECK(restored.read_vram(2U) == 0x44U);
    CHECK(frame_has_nonblack(restored.framebuffer()));
}
