#include "nes_system.hpp"

#include "fds.hpp" // FDS disk-image detection + the RP2C33 RAM-adapter mapper

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
        img.battery = (flags6 & 0x02U) != 0U; // battery-backed $6000-$7FFF RAM
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

    void nes_system::set_zapper(int x, int y, bool trigger) noexcept {
        zapper_x = static_cast<std::int16_t>(x);
        zapper_y = static_cast<std::int16_t>(y);
        zapper_trigger = trigger;
    }

    bool nes_system::zapper_light_detected() const noexcept {
        const auto fb = ppu.framebuffer();
        if (fb.pixels == nullptr || zapper_x < 0 || zapper_y < 0 ||
            static_cast<std::uint32_t>(zapper_x) >= fb.width ||
            static_cast<std::uint32_t>(zapper_y) >= fb.height) {
            return false; // off-screen: the photodiode sees no CRT light
        }
        const std::uint32_t px =
            fb.pixels[static_cast<std::size_t>(zapper_y) * fb.effective_stride() +
                      static_cast<std::size_t>(zapper_x)];
        const std::uint32_t r = (px >> 16U) & 0xFFU;
        const std::uint32_t g = (px >> 8U) & 0xFFU;
        const std::uint32_t b = px & 0xFFU;
        // The gun trips on a bright spot (white / light colours the games flash over
        // a target); average the channels and threshold above mid-grey.
        return (r + g + b) >= 0x180U;
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
        // The Zapper occupies port 2 ($4017): light-sense bit 3 (0 = light) + trigger
        // bit 4 (1 = pulled), no serial shift.
        if (zapper_enabled && (port & 1) == 1) {
            std::uint8_t z = 0x40U; // open-bus high bits, as for a pad read
            if (zapper_trigger) {
                z |= 0x10U;
            }
            if (!zapper_light_detected()) {
                z |= 0x08U; // bit 3 set = NO light
            }
            return z;
        }
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
                                             const nes_config& config) {
        auto sys = std::make_unique<nes_system>();
        nes_system* s = sys.get();

        // A Famicom Disk System disk image (.fds) plus a BIOS routes to the RP2C33
        // RAM adapter instead of a cartridge; everything else is an iNES cart.
        const bool is_fds = !config.fds_bios.empty() && looks_like_fds(rom);
        ines_image img{};
        if (is_fds) {
            s->is_fds = true;
            s->fds_bios = config.fds_bios;
            s->fds_disk = parse_fds_sides(rom);
            s->fds_ram.assign(0x8000U, 0x00U); // 32 KiB PRG-RAM ($6000-$DFFF)
            s->chr.assign(0x2000U, 0x00U);     // 8 KiB CHR-RAM
            s->chr_is_ram = true;
            s->ppu.set_mirroring(mirroring::horizontal);
        } else {
            img = parse_ines(rom);
            s->prg = img.prg; // empty on a bad/unsupported image -> boots a blank PRG
            s->chr = img.chr;
            s->chr_is_ram = img.chr_is_ram; // CHR-RAM is writable -> include in save-states
            s->battery = img.battery;       // persist $6000 RAM only for battery carts
            s->ppu.set_mirroring(img.valid ? img.mirroring : mirroring::horizontal);
        }

        // Region timing: PAL gives the PPU a 312-line/50 Hz frame and shifts the APU
        // frame-sequencer period + DMC rate table (the 2A07). NTSC is the default.
        const bool pal = config.video_region == mnemos::video_region::pal;
        s->ppu.set_pal(pal);
        s->apu.set_pal(pal);

        s->zapper_enabled = config.zapper; // Zapper light gun on port 2

        // The 2A03 is a 6502 with no on-chip I/O port: $0000/$0001 are plain RAM.
        s->cpu.set_port_enabled(false);

        // The APU's frame sequencer is CPU-clocked (it ticks once per CPU cycle in
        // the schedule). One native audio sample per 37 CPU cycles puts the native
        // rate (1.789773 MHz / 37 ~= 48.4 kHz) just above the 48 kHz output, so the
        // adapter down-resamples cleanly. (NTSC; PAL timing is a later increment.)
        s->apu.set_clock_divider(37);

        // The DMC channel streams delta-PCM samples from cartridge ROM
        // ($8000-$FFFF) via the CPU bus during playback.
        s->apu.set_dmc_reader([s](std::uint16_t addr) { return s->bus.read8(addr); });

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

        if (is_fds) {
            // The RP2C33 disk system maps $6000-$DFFF RAM, $E000-$FFFF BIOS, the
            // $4020-$409F disk/sound registers and attaches the CHR-RAM itself.
            s->mapper =
                make_fds(s->bus, s->ppu, std::span<const std::uint8_t>(s->fds_disk),
                         std::span<const std::uint8_t>(s->fds_bios),
                         std::span<std::uint8_t>(s->fds_ram), std::span<std::uint8_t>(s->chr));
        } else {
            // $6000-$7FFF: 8 KiB cartridge work / battery RAM. Always present -- many
            // mapper games (MMC3, MMC1 SRAM titles) keep work variables here and break
            // reading open bus; harmless for carts that ignore it.
            s->bus.map_ram(0x6000U, std::span<std::uint8_t>(s->prg_ram));

            // $8000-$FFFF (PRG) + the PPU's CHR window are owned by the cartridge
            // mapper; reset() installs the initial banks (and CHR-RAM vs CHR-ROM).
            s->mapper = make_mapper(img.valid ? img.mapper : 0, s->bus, s->ppu,
                                    std::span<const std::uint8_t>(s->prg),
                                    std::span<std::uint8_t>(s->chr), img.chr_is_ram);
        }
        s->mapper->reset();

        // The cartridge (MMC3 scanline counter) and the APU (frame + DMC IRQ) share
        // the CPU /IRQ line as a wired-OR: each source updates its flag and the
        // union is published to the CPU.
        s->mapper->set_irq_callback([s](bool asserted) {
            s->mapper_irq = asserted;
            s->cpu.set_irq_line(s->mapper_irq || s->apu_irq);
        });
        s->apu.set_irq_callback([s](bool asserted) {
            s->apu_irq = asserted;
            s->cpu.set_irq_line(s->mapper_irq || s->apu_irq);
        });

        // The PPU drives the CPU /NMI: it asserts at vblank and clears at
        // pre-render. The scanline callback also clocks the mapper's scanline IRQ.
        // Both callbacks republish the PPU's current NMI level; the CPU latches the
        // inactive->active edge.
        s->ppu.set_vblank_callback(
            [s](std::uint32_t) { s->cpu.set_nmi_line(s->ppu.nmi_asserted()); });
        // The Sunsoft FME-7 IRQ counter free-runs on the CPU clock; approximate it at
        // scanline granularity (NTSC ~113.7, PAL ~106.6 CPU cycles per line).
        const std::uint32_t cpu_cycles_per_line = pal ? 107U : 114U;
        s->ppu.set_scanline_callback([s, cpu_cycles_per_line](std::uint32_t line) {
            s->cpu.set_nmi_line(s->ppu.nmi_asserted());
            // The FME-7 counter ticks regardless of rendering (it is not A12-driven).
            s->mapper->clock_cpu_timer(cpu_cycles_per_line);
            // The MMC3 scanline counter is driven by PPU A12 toggles, which only
            // occur while rendering; a blanked screen does not clock it.
            if (s->ppu.rendering_enabled()) {
                s->mapper->clock_scanline(line);
            }
        });

        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on); // loads PC from the $FFFC reset vector
        return sys;
    }

} // namespace mnemos::manifests::nes
