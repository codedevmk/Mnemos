#include "capcom_cps1_system.hpp"

#include <span>
#include <utility>

namespace mnemos::manifests::capcom_cps1 {

    namespace {
        // The named region's bytes, padded to `size` so the bus map always has
        // full-size backing (absent dumps read 0xFF).
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, const std::string& name, std::size_t size) {
            auto& bytes = image.regions[name];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }
    } // namespace

    cps1_board_params board_params_for(std::string_view /*set_name*/) {
        // Increment 1 carries no per-set wiring beyond the legacy default profile;
        // the romset TOML profile key that selects a board profile lands later.
        return {};
    }

    cps1_system::cps1_system(common::rom_set_image image, cps1_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        // --- main bus: program ROM low, RAM overlays above it ---
        auto& program = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(program_rom_base, std::span<const std::uint8_t>(program), 0);
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_bus.map_ram(gfx_ram_base, gfx_ram, 1);

        // CPS-A register file: the board writes the raw 32-word window.
        main_bus.map_mmio(
            cps_a_reg_base, cps_a_reg_size,
            [this](std::uint32_t address) -> std::uint8_t {
                const std::size_t idx = (address - cps_a_reg_base) >> 1U;
                if (idx >= cps_a_reg_count) {
                    return 0xFFU;
                }
                const std::uint16_t word = cps_a_regs[idx];
                return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                            : static_cast<std::uint8_t>(word);
            },
            [this](std::uint32_t address, std::uint8_t value) {
                const std::size_t idx = (address - cps_a_reg_base) >> 1U;
                if (idx >= cps_a_reg_count) {
                    return;
                }
                std::uint16_t& word = cps_a_regs[idx];
                word = (address & 1U) == 0U
                           ? static_cast<std::uint16_t>((word & 0x00FFU) | (value << 8U))
                           : static_cast<std::uint16_t>((word & 0xFF00U) | value);
            },
            1);

        // CPS-B register file: reads/writes go through the video chip's raw
        // register file (the active profile interprets it).
        main_bus.map_mmio(
            cps_b_reg_base, cps_b_reg_size,
            [this](std::uint32_t address) -> std::uint8_t {
                const std::uint8_t idx =
                    static_cast<std::uint8_t>((address - cps_b_reg_base) >> 1U);
                const std::uint16_t word = video.cps_b_reg(idx);
                return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                            : static_cast<std::uint8_t>(word);
            },
            [this](std::uint32_t address, std::uint8_t value) {
                const std::uint8_t idx =
                    static_cast<std::uint8_t>((address - cps_b_reg_base) >> 1U);
                const std::uint16_t word = video.cps_b_reg(idx);
                const std::uint16_t merged =
                    (address & 1U) == 0U
                        ? static_cast<std::uint16_t>((word & 0x00FFU) | (value << 8U))
                        : static_cast<std::uint16_t>((word & 0xFF00U) | value);
                video.set_cps_b_reg(idx, merged);
            },
            1);

        main_cpu.attach_bus(main_bus);

        // --- video: GFX ROM + unified GFX RAM + the board-owned palette ---
        const auto& gfx = pinned_region(roms, "gfx", 0U);
        video.attach_gfx(std::span<const std::uint8_t>(gfx));
        video.attach_tile_ram(gfx_ram);
        video.attach_object_ram(gfx_ram);
        video.attach_palette(palette);
        video.set_cps_b_profile(profile_for_id(params.cps_b_profile_id)
                                    .value_or(chips::video::cps_a_b::cps_b_profile{}));

        // Construction order: ROM mapped + bus attached, then power-on reset so the
        // 68K loads SSP/PC from the cart's reset vectors.
        main_cpu.reset(chips::reset_kind::power_on);
        video.reset(chips::reset_kind::power_on);
    }

    void cps1_system::run_frame() {
        // CPS1: ~10 MHz 68K at ~59.6 Hz. Exact CPU<->beam sync is a later
        // increment; the two chips tick a frame's worth decoupled here.
        constexpr std::uint64_t cpu_cycles_per_frame = 10'000'000ULL / 60ULL;
        constexpr std::uint64_t video_dots_per_frame =
            static_cast<std::uint64_t>(chips::video::cps_a_b::frame_lines) *
            chips::video::cps_a_b::line_pixels;
        main_cpu.tick(cpu_cycles_per_frame);
        video.tick(video_dots_per_frame);
    }

    common::rom_set_decl cps1_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.board = "capcom_cps1";
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        return decl;
    }

    std::unique_ptr<cps1_system> assemble_cps1(common::rom_set_image image,
                                               cps1_board_params board_params) {
        return std::make_unique<cps1_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::capcom_cps1
