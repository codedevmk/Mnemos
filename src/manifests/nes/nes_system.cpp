#include "nes_system.hpp"

namespace mnemos::manifests::nes {

    namespace {
        using mirroring = chips::video::ppu2c02::mirroring;

        constexpr std::size_t k_header = 16U;
        constexpr std::size_t k_trainer = 512U;
        constexpr std::size_t k_prg_unit = 0x4000U; // 16 KiB
        constexpr std::size_t k_chr_unit = 0x2000U; // 8 KiB
    } // namespace

    ines_image parse_ines(std::span<const std::uint8_t> data) {
        ines_image img;
        if (data.size() < k_header || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' ||
            data[3] != 0x1AU) {
            return img; // not an iNES image
        }
        const std::size_t prg_count = data[4];
        const std::size_t chr_count = data[5];
        const std::uint8_t flags6 = data[6];
        const std::uint8_t flags7 = data[7];

        img.mapper = static_cast<int>((flags7 & 0xF0U) | (flags6 >> 4U));
        if ((flags6 & 0x08U) != 0U) {
            img.mirroring = mirroring::four_screen;
        } else {
            img.mirroring = (flags6 & 0x01U) != 0U ? mirroring::vertical : mirroring::horizontal;
        }

        const std::size_t offset = k_header + ((flags6 & 0x04U) != 0U ? k_trainer : 0U);
        const std::size_t prg_size = prg_count * k_prg_unit;
        const std::size_t chr_size = chr_count * k_chr_unit;
        if (prg_count == 0U || data.size() < offset + prg_size + chr_size) {
            return img; // malformed sizes
        }

        img.prg.assign(data.data() + offset, data.data() + offset + prg_size);
        if (chr_count == 0U) {
            img.chr_is_ram = true;
            img.chr.assign(k_chr_unit, 0x00U); // 8 KiB CHR-RAM (not yet writable via the PPU)
        } else {
            img.chr.assign(data.data() + offset + prg_size,
                           data.data() + offset + prg_size + chr_size);
        }
        img.valid = true;
        return img;
    }

    void nes_system::set_pad(int port, std::uint8_t buttons) noexcept {
        if (port == 0 || port == 1) {
            pad_buttons[static_cast<std::size_t>(port)] = buttons;
        }
    }

    void nes_system::write_controller_strobe(std::uint8_t value) noexcept {
        pad_strobe = (value & 0x01U) != 0U;
        if (pad_strobe) {
            // While the strobe is high the registers track the live state; the
            // 1->0 edge games use latches the value reloaded here.
            pad_shift[0] = pad_buttons[0];
            pad_shift[1] = pad_buttons[1];
        }
    }

    std::uint8_t nes_system::read_controller(int port) noexcept {
        const auto p = static_cast<std::size_t>(port & 1);
        std::uint8_t bit;
        if (pad_strobe) {
            bit = pad_buttons[p] & 0x01U; // strobing returns A continuously
        } else {
            bit = pad_shift[p] & 0x01U;
            // Shift right, clocking in 1s so reads past the 8th return 1 (open bus).
            pad_shift[p] = static_cast<std::uint8_t>((pad_shift[p] >> 1U) | 0x80U);
        }
        return static_cast<std::uint8_t>(bit | 0x40U); // open-bus high bits
    }

    std::unique_ptr<nes_system> assemble_nes(std::span<const std::uint8_t> rom,
                                             const nes_config& /*config*/) {
        auto sys = std::make_unique<nes_system>();
        nes_system* s = sys.get();

        const ines_image img = parse_ines(rom);
        s->prg = img.prg; // empty on a bad/unsupported image -> boots a blank PRG
        s->chr = img.chr;
        s->ppu.set_mirroring(img.valid ? img.mirroring : mirroring::horizontal);

        // The 2A03 is a 6502 with no on-chip I/O port: $0000/$0001 are plain RAM.
        s->cpu.set_port_enabled(false);

        // The APU's frame sequencer is CPU-clocked (it ticks once per CPU cycle in
        // the schedule). One native audio sample per 37 CPU cycles puts the native
        // rate (1.789773 MHz / 37 ~= 48.4 kHz) just above the 48 kHz output, so the
        // adapter down-resamples cleanly. (NTSC; PAL timing is a later increment.)
        s->apu.set_clock_divider(37);

        // $0000-$1FFF: 2 KiB work RAM, mirrored four times (A11/A12 ignored).
        for (std::uint32_t base = 0x0000U; base < 0x2000U; base += 0x0800U) {
            s->bus.map_ram(base, std::span<std::uint8_t>(s->wram));
        }

        // $2000-$3FFF: the PPU's eight registers, mirrored every 8 bytes.
        s->bus.map_mmio(
            0x2000U, 0x2000U,
            [s](std::uint32_t addr) -> std::uint8_t {
                return s->ppu.reg_read(static_cast<std::uint8_t>(addr & 0x07U));
            },
            [s](std::uint32_t addr, std::uint8_t value) {
                s->ppu.reg_write(static_cast<std::uint8_t>(addr & 0x07U), value);
            });

        // $4000-$401F: APU + I/O. $4014 = OAM DMA, $4016/$4017 = controllers
        // (a later increment); everything else is the APU. Note $4017 is the APU
        // frame counter on write but controller 2 on read.
        s->bus.map_mmio(
            0x4000U, 0x0020U,
            [s](std::uint32_t addr) -> std::uint8_t {
                if (addr == 0x4015U) {
                    return s->apu.read_reg(static_cast<std::uint16_t>(addr));
                }
                if (addr == 0x4016U) {
                    return s->read_controller(0);
                }
                if (addr == 0x4017U) {
                    return s->read_controller(1);
                }
                return 0x00U;
            },
            [s](std::uint32_t addr, std::uint8_t value) {
                if (addr == 0x4014U) {
                    // OAM DMA: copy 256 bytes from CPU page (value<<8) into sprite
                    // RAM. Games set OAMADDR ($2003) to 0 first, so DMA from index 0.
                    const auto base =
                        static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) << 8U);
                    for (std::uint32_t i = 0U; i < 256U; ++i) {
                        s->ppu.poke_oam(i, s->bus.read8(static_cast<std::uint32_t>(base + i)));
                    }
                    return;
                }
                if (addr == 0x4016U) {
                    s->write_controller_strobe(value); // both pads share the strobe
                    return;
                }
                s->apu.write_reg(static_cast<std::uint16_t>(addr), value);
            });

        // $8000-$FFFF (PRG) + the PPU's CHR window are owned by the cartridge
        // mapper; reset() installs the initial banks (and CHR-RAM vs CHR-ROM).
        s->mapper = make_mapper(img.valid ? img.mapper : 0, s->bus, s->ppu,
                                std::span<const std::uint8_t>(s->prg),
                                std::span<std::uint8_t>(s->chr), img.chr_is_ram);
        s->mapper->reset();

        // The PPU drives the CPU /NMI: it asserts at vblank and clears at
        // pre-render. Both callbacks republish the PPU's current NMI level; the
        // CPU latches the inactive->active edge.
        s->ppu.set_vblank_callback(
            [s](std::uint32_t) { s->cpu.set_nmi_line(s->ppu.nmi_asserted()); });
        s->ppu.set_scanline_callback(
            [s](std::uint32_t) { s->cpu.set_nmi_line(s->ppu.nmi_asserted()); });

        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on); // loads PC from the $FFFC reset vector
        return sys;
    }

} // namespace mnemos::manifests::nes
