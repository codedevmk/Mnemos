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

    m72_board_params board_params_for(std::string_view set_name) {
        // Work-RAM wiring + factory DIP defaults per declared set. The base
        // map (work RAM at 0xA0000) covers the m72_map wiring variant.
        if (set_name == "rtype") {
            return {.work_ram_base = 0x40000U, .dip_default = 0xFDFBU};
        }
        return {};
    }

    m72_system::m72_system(common::rom_set_image image, m72_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        dip_switches = params.dip_default;

        // --- main bus: program ROM under the RAM overlays ---
        auto& main_prog = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(0x00000U, std::span<const std::uint8_t>(main_prog));
        main_bus.map_ram(sprite_ram_base, sprite_ram, 1);
        main_bus.map_ram(palette_a_base, palette_a, 1);
        main_bus.map_ram(palette_b_base, palette_b, 1);
        main_bus.map_ram(vram_a_base, vram_a, 1);
        main_bus.map_ram(vram_b_base, vram_b, 1);
        main_bus.map_ram(params.work_ram_base, work_ram, 1);
        // The Z80's program RAM, byte-visible to the V30: the boot path
        // uploads the sound program here before releasing the Z80.
        main_bus.map_ram(sound_ram_window, sound_ram, 1);
        main_cpu.attach_bus(main_bus);

        // --- sound bus: the Z80 executes entirely from the shared RAM ---
        sound_bus.map_ram(0x0000U, sound_ram, 0);
        sound_cpu.attach_bus(sound_bus);

        // --- V30 I/O space ---
        main_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case port_in_p1:
                return input_p1;
            case port_in_p2:
                return input_p2;
            case port_in_system:
                // Bit 7 is the sprite-DMA-complete flag; the copy is
                // instantaneous here, so it always reads done (inactive).
                return input_system;
            case port_in_system + 1U:
                return 0xFFU;
            case port_in_dsw_lo:
                return static_cast<std::uint8_t>(dip_switches);
            case port_in_dsw_hi:
                return static_cast<std::uint8_t>(dip_switches >> 8U);
            case port_pic_a0:
                return pic.read(0U);
            case port_pic_a1:
                return pic.read(1U);
            case port_mcu_latch:
                return mcu_to_main;
            default:
                return 0xFFU; // open bus
            }
        });
        main_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            const std::uint16_t p = port & 0xFFU;
            if (p >= port_out_scroll_base && p < port_out_scroll_base + scroll_regs.size()) {
                scroll_regs[p - port_out_scroll_base] = value;
                const auto word = [this](std::size_t i) {
                    return static_cast<std::uint16_t>(scroll_regs[i] | (scroll_regs[i + 1U] << 8U));
                };
                video.set_scroll_a(word(2U), word(0U));
                video.set_scroll_b(word(6U), word(4U));
                return;
            }
            switch (p) {
            case port_out_sound_latch:
                sound_latch = value;
                sound_latch_irq = true; // latch write knocks on the Z80
                update_sound_irq();
                break;
            case port_out_control:
                control_register = value;
                // Bit 3: display disable. Bit 4: sound-CPU /RESET, active
                // low -- the V30 raises it once the sound program upload is
                // done. Bits 0/1 coin counters, bit 2 flip-screen: latched,
                // semantics with the cabinet pass.
                video.set_display_enable((value & 0x08U) == 0U);
                sound_cpu.set_reset_line((value & 0x10U) == 0U);
                break;
            case port_out_sprite_dma:
                video.latch_sprites();
                break;
            case port_out_raster_lo:
            case port_out_raster_hi: {
                raster_regs[p - port_out_raster_lo] = value;
                const int line =
                    static_cast<int>((raster_regs[0] | (raster_regs[1] << 8U)) & 0x1FFU) - 128;
                video.set_raster_compare(line);
                // The write also withdraws a pending, unserviced request.
                pic.set_irq_line(2U, false);
                break;
            }
            case port_pic_a0:
                pic.write(0U, value);
                break;
            case port_pic_a1:
                pic.write(1U, value);
                break;
            case port_mcu_latch:
                main_to_mcu = value;
                if (mcu_present) { // knock: edge-pulse the MCU's INT1
                    mcu.set_int1_line(true);
                    mcu.set_int1_line(false);
                }
                break;
            default:
                break; // remaining board ports land with their subsystems
            }
        });

        // --- Z80 I/O space: YM2151 at 0/1, the latch at 2, its interrupt
        // acknowledge at 6 ---
        sound_cpu.set_port_in([this](std::uint16_t port) -> std::uint8_t {
            switch (port & 0xFFU) {
            case z80_port_ym2151_addr:
            case z80_port_ym2151_data:
                return fm.read_status();
            case z80_port_latch:
                return sound_latch;
            case z80_port_sample_read: {
                const auto& samples = roms.regions["samples"];
                if (samples.empty()) {
                    return 0xFFU;
                }
                const std::uint8_t byte = samples[sample_address % samples.size()];
                ++sample_address;
                return byte;
            }
            default:
                return 0xFFU;
            }
        });
        sound_cpu.set_port_out([this](std::uint16_t port, std::uint8_t value) {
            switch (port & 0xFFU) {
            case z80_port_ym2151_addr:
                fm.write_address(value);
                break;
            case z80_port_ym2151_data:
                fm.write_data(value);
                break;
            case z80_port_latch_ack:
                sound_latch_irq = false;
                update_sound_irq();
                break;
            case z80_port_sample_addr_lo:
                sample_address = static_cast<std::uint16_t>((sample_address & 0xFF00U) | value);
                break;
            case z80_port_sample_addr_hi:
                sample_address =
                    static_cast<std::uint16_t>((sample_address & 0x00FFU) | (value << 8U));
                break;
            case z80_port_dac:
                dac.write(value);
                break;
            default:
                break;
            }
        });
        // The IM0 jam byte: the pending sources' RST opcodes AND together
        // (open-collector negative buffer); idle floats to RST 38h.
        sound_cpu.set_irq_vector([this]() -> std::uint8_t {
            std::uint8_t vector = z80_rst_idle;
            if (sound_latch_irq) {
                vector &= z80_rst_latch;
            }
            if (fm.irq_asserted()) {
                vector &= z80_rst_ym;
            }
            return vector;
        });
        fm.set_irq([this](bool) { update_sound_irq(); });

        // --- optional protection MCU: program from the "mcu" region, the
        // sample ROM and the latch pair on its external (MOVX) bus ---
        const auto& mcu_program = roms.regions["mcu"];
        mcu_present = !mcu_program.empty();
        if (mcu_present) {
            mcu.attach_program(mcu_program);
            const auto& samples = roms.regions["samples"];
            if (!samples.empty()) {
                mcu_bus.map_rom(0x0000U, samples);
            }
            mcu_bus.map_mmio(
                mcu_latch_in, 2U,
                [this](std::uint32_t address) -> std::uint8_t {
                    return address == mcu_latch_in ? main_to_mcu : mcu_to_main;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    if (address == mcu_latch_out) {
                        mcu_to_main = value;
                    } else {
                        main_to_mcu = value;
                    }
                },
                1);
            mcu.attach_bus(mcu_bus);
        }

        // --- video: VRAM/palette/tile spans + the scanline pulses into the
        // interrupt controller (IR0 = vblank line, IR2 = raster compare; each
        // asserted at its line and withdrawn at the next) ---
        video.attach_vram_a(vram_a);
        video.attach_vram_b(vram_b);
        video.attach_palette_a(palette_a);
        video.attach_palette_b(palette_b);
        video.attach_sprite_ram(sprite_ram);
        video.attach_tiles_a(roms.regions["tiles_a"]);
        video.attach_tiles_b(roms.regions["tiles_b"]);
        video.attach_sprites(roms.regions["sprites"]);
        video.set_scanline_callback([this](std::uint32_t line) {
            pic.set_irq_line(0U, line == chips::video::irem_m72_video::visible_height);
            pic.set_irq_line(2U, video.raster_compare_matches(line));
        });
        pic.set_int_callback([this](bool asserted) { main_cpu.set_irq_line(asserted); });
        main_cpu.set_irq_ack([this]() -> std::uint8_t { return pic.acknowledge(); });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        // No sound ROM exists: the Z80 waits in reset until the V30 uploads
        // its program into the shared RAM and releases it via port 0x02.
        sound_cpu.set_reset_line(true);
    }

    common::rom_set_decl m72_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        return decl;
    }

    std::unique_ptr<m72_system> assemble_m72(common::rom_set_image image,
                                             m72_board_params board_params) {
        return std::make_unique<m72_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::irem_m72
