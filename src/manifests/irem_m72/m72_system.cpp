#include "m72_system.hpp"

#include <span>
#include <utility>

namespace mnemos::manifests::irem_m72 {

    namespace {
        // Return the named region's bytes, padded/created at `size` so the bus
        // map below always has full-size backing (absent dumps read 0xFF).
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, const std::string& name, std::size_t size) {
            auto& bytes = image.regions[name];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }
    } // namespace

    m72_system::m72_system(common::rom_set_image image) : roms(std::move(image)) {
        // --- main bus: program ROM under the RAM overlays ---
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(0x00000U, std::span<const std::uint8_t>(main_prog));
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_ram(palette_a_base, palette_a, 1);
        main_bus.map_ram(palette_b_base, palette_b, 1);
        main_bus.map_ram(vram_a_base, vram_a, 1);
        main_bus.map_ram(vram_b_base, vram_b, 1);
        main_bus.map_ram(work_ram_base, work_ram, 1);
        main_cpu.attach_bus(main_bus);

        // --- sound bus: Z80 program ROM with its work RAM on top ---
        auto& sound_prog = pinned_region(roms, "soundcpu", sound_rom_size);
        sound_bus.map_rom(0x0000U, std::span<const std::uint8_t>(sound_prog));
        sound_bus.map_ram(sound_ram_base, sound_ram, 1);
        sound_cpu.attach_bus(sound_bus);

        // --- V30 I/O space: inputs/DIPs in, sound latch + board register out ---
        main_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case port_in_p1:
                return input_p1;
            case port_in_p2:
                return input_p2;
            case port_in_system:
                return input_system;
            case port_in_dsw_lo:
                return static_cast<std::uint8_t>(dip_switches);
            case port_in_dsw_hi:
                return static_cast<std::uint8_t>(dip_switches >> 8U);
            default:
                return 0xFFU; // open bus
            }
        });
        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            switch (port & 0xFFU) {
            case port_out_sound_latch:
                sound_latch = value;
                sound_cpu.set_irq_line(true); // latch write knocks on the Z80
                break;
            case port_out_flip_coin:
                flip_coin_register = value; // semantics land with video/inputs
                break;
            default:
                break; // remaining board ports land with their subsystems
            }
        });

        // --- Z80 I/O space: the latch read acknowledges the INT line; the
        // YM2151 window reads open until the audio phase wires the chip ---
        sound_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            if ((port & 0xFFU) == z80_port_latch) {
                sound_cpu.set_irq_line(false);
                return sound_latch;
            }
            return 0xFFU;
        });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
    }

    common::rom_set_decl m72_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        decl.regions.push_back(
            {.name = "soundcpu", .size = sound_rom_size, .fill = 0xFFU, .files = {}});
        return decl;
    }

    std::unique_ptr<m72_system> assemble_m72(common::rom_set_image image) {
        return std::make_unique<m72_system>(std::move(image));
    }

} // namespace mnemos::manifests::irem_m72
