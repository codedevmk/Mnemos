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
                if (addr == 0x4016U || addr == 0x4017U) {
                    return 0x40U; // open-bus high bit set; controllers arrive later
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
                    return; // controller strobe -- a later increment
                }
                s->apu.write_reg(static_cast<std::uint16_t>(addr), value);
            });

        // $8000-$FFFF: PRG-ROM. A 16 KiB image mirrors into both halves (so the
        // $FFFC reset vector resolves); a 32 KiB image fills the whole range.
        if (!s->prg.empty()) {
            s->bus.map_rom(0x8000U, std::span<const std::uint8_t>(s->prg));
            if (s->prg.size() <= k_prg_unit) {
                s->bus.map_rom(0xC000U, std::span<const std::uint8_t>(s->prg));
            }
        }

        if (!s->chr.empty()) {
            s->ppu.attach_chr(std::span<const std::uint8_t>(s->chr));
        }

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
