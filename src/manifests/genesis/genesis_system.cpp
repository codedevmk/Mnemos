#include "genesis_system.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>

namespace mnemos::manifests::genesis {

    namespace {
        // Opt-in WRAM-write watcher. MNEMOS_WRAM_WATCH=1 enables; LO/HI override
        // the address range (hex), F_LO/F_HI the frame range (decimal). Each
        // qualifying write logs "[wram] f=N pc=$XXXXXX [$YYYYYY]=ZZ" to stderr.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv -- opt-in debug only
#endif
        struct watch_cfg {
            bool enabled{};
            std::uint32_t lo{0xE8E0U};
            std::uint32_t hi{0xE910U};
            std::uint64_t f_lo{0U};
            std::uint64_t f_hi{~std::uint64_t{0}};
        };

        [[nodiscard]] std::uint32_t parse_hex_env(const char* name, std::uint32_t fallback) {
            const char* v = std::getenv(name);
            if (v == nullptr || *v == '\0') {
                return fallback;
            }
            return static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16));
        }
        [[nodiscard]] std::uint64_t parse_dec_env(const char* name, std::uint64_t fallback) {
            const char* v = std::getenv(name);
            if (v == nullptr || *v == '\0') {
                return fallback;
            }
            return std::strtoull(v, nullptr, 10);
        }
        [[nodiscard]] watch_cfg load_watch_cfg() {
            watch_cfg cfg;
            const char* en = std::getenv("MNEMOS_WRAM_WATCH");
            cfg.enabled = en != nullptr && en[0] != '\0' && en[0] != '0';
            cfg.lo = parse_hex_env("MNEMOS_WRAM_WATCH_LO", cfg.lo);
            cfg.hi = parse_hex_env("MNEMOS_WRAM_WATCH_HI", cfg.hi);
            cfg.f_lo = parse_dec_env("MNEMOS_WRAM_WATCH_F_LO", cfg.f_lo);
            cfg.f_hi = parse_dec_env("MNEMOS_WRAM_WATCH_F_HI", cfg.f_hi);
            return cfg;
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    } // namespace


    std::unique_ptr<genesis_system> assemble_genesis(std::vector<std::uint8_t> rom,
                                                     const genesis_config& config) {
        auto sys = std::make_unique<genesis_system>();
        genesis_system* s = sys.get();
        s->rom = std::move(rom);

        const bool pal = config.video_region == mnemos::video_region::pal;
        s->vdp.set_pal(pal);

        // $A10001 version: bit7 = export, bit6 = PAL, bit5 = no expansion unit.
        s->version_register = static_cast<std::uint8_t>(0x80U | (pal ? 0x40U : 0x00U) | 0x20U);

        // $000000-$3FFFFF: cartridge ROM.
        s->bus.map_rom(0x000000U, std::span<const std::uint8_t>(s->rom), 0);

        // $A00000-$A03FFF: Z80 RAM (8 KiB, mirrored).
        s->bus.map_mmio(
            0xA00000U, 0x4000U, [s](std::uint32_t a) { return s->z80_ram[a & 0x1FFFU]; },
            [s](std::uint32_t a, std::uint8_t v) { s->z80_ram[a & 0x1FFFU] = v; }, 0);

        // $A04000-$A05FFF: YM2612 ports (addr/data x 2, mirrored).
        s->bus.map_mmio(
            0xA04000U, 0x2000U, [s](std::uint32_t /*a*/) { return s->fm.read_status(); },
            [s](std::uint32_t a, std::uint8_t v) {
                const int port = static_cast<int>((a >> 1U) & 1U);
                const bool data = (a & 1U) != 0U;
                s->fm.write(port, data, v);
            },
            0);

        // $A10000-$A1001F: I/O sub-controller. $A10001=version (RO),
        // $A10003/05/07=data A/B/C, $A10009/0B/0D=ctrl A/B/C, rest=serial regs.
        // Unwritten bytes must read 0 (the ROM's reset-entry TST relies on it).
        s->bus.map_mmio(
            0xA10000U, 0x20U,
            [s](std::uint32_t a) -> std::uint8_t {
                const std::uint32_t off = a & 0x1FU;
                switch (off) {
                case 0x01:
                    return s->version_register;
                case 0x03:
                    return s->read_pad_port(0);
                case 0x05:
                    return s->read_pad_port(1);
                default:
                    return s->io_regs[off];
                }
            },
            [s](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t off = a & 0x1FU;
                // Bit 6 of a data-port write is the TH select line.
                if (off == 0x03) {
                    s->pad_th[0] = (v & 0x40U) != 0U;
                } else if (off == 0x05) {
                    s->pad_th[1] = (v & 0x40U) != 0U;
                }
                // Echo every write back (read-only $A10001 excepted).
                if (off != 0x01) {
                    s->io_regs[off] = v;
                }
            },
            0);

        // $A11100 Z80 BUSREQ. Bit 0 reads 1 while the Z80 still holds its bus.
        s->bus.map_mmio(
            0xA11100U, 0x2U,
            [s](std::uint32_t a) -> std::uint8_t {
                return (a & 1U) == 0U ? static_cast<std::uint8_t>(s->z80_running ? 0x01U : 0x00U)
                                      : 0x00U;
            },
            [s](std::uint32_t a, std::uint8_t v) {
                if ((a & 1U) == 0U) {
                    s->z80_bus_requested = (v & 0x01U) != 0U;
                    s->z80_running = s->z80_reset_released && !s->z80_bus_requested;
                }
            },
            0);

        // $A11200 Z80 RESET. Bit 0: 0 = held in reset, 1 = released.
        s->bus.map_mmio(
            0xA11200U, 0x2U, [](std::uint32_t /*a*/) -> std::uint8_t { return 0x00U; },
            [s](std::uint32_t a, std::uint8_t v) {
                if ((a & 1U) != 0U) {
                    return;
                }
                const bool released = (v & 0x01U) != 0U;
                if (!released && s->z80_reset_released) {
                    s->z80.reset(chips::reset_kind::power_on); // falling edge
                }
                s->z80_reset_released = released;
                s->z80_running = s->z80_reset_released && !s->z80_bus_requested;
            },
            0);

        // $C00000-$C0001F: VDP ports + the SN76489 PSG at $C00011. 68K word
        // accesses split into even (high) + odd (low) bytes we coalesce.
        s->bus.map_mmio(
            0xC00000U, 0x20U,
            [s](std::uint32_t a) -> std::uint8_t {
                const std::uint32_t offset = a & 0x1FU;
                if ((offset & 1U) == 0U) {
                    const std::uint16_t word = s->vdp.read16(offset);
                    s->vdp_read_low = static_cast<std::uint8_t>(word);
                    return static_cast<std::uint8_t>(word >> 8U);
                }
                return s->vdp_read_low;
            },
            [s](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t offset = a & 0x1FU;
                if (offset == 0x11U) {
                    s->psg.write(v);
                    return;
                }
                if ((offset & 1U) == 0U) {
                    s->vdp_write_high = v;
                } else {
                    s->vdp.write16(offset & ~1U,
                                   static_cast<std::uint16_t>(
                                       (static_cast<std::uint16_t>(s->vdp_write_high) << 8U) | v));
                }
            },
            0);

        // $E00000-$FFFFFF: 68K work RAM (64 KiB, mirrored). The write lambda
        // also routes to the opt-in WRAM watcher.
        const watch_cfg watch = load_watch_cfg();
        s->bus.map_mmio(
            0xE00000U, 0x200000U, [s](std::uint32_t a) { return s->work_ram[a & 0xFFFFU]; },
            [s, watch](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t wa = a & 0xFFFFU;
                s->work_ram[wa] = v;
                if (watch.enabled && wa >= watch.lo && wa <= watch.hi &&
                    s->frame_index >= watch.f_lo && s->frame_index <= watch.f_hi) {
                    const auto pc = s->cpu.cpu_registers().pc;
                    std::fprintf(stderr, "[wram] f=%llu pc=$%06X [$%04X]=%02X\n",
                                 static_cast<unsigned long long>(s->frame_index),
                                 static_cast<unsigned>(pc & 0xFFFFFFU),
                                 static_cast<unsigned>(wa), static_cast<unsigned>(v));
                }
            },
            0);

        s->vdp.set_dma_read([s](std::uint32_t addr) -> std::uint16_t {
            const auto hi = s->bus.read8(addr & 0xFFFFFFU);
            const auto lo = s->bus.read8((addr + 1U) & 0xFFFFFFU);
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8U) | lo);
        });

        s->vdp.set_irq_callback([s](int level) { s->cpu.set_irq_level(level); });
        // The 68K IACK clears the VDP IRQ request (many V-blank handlers rely
        // on this instead of acking via the status read).
        s->cpu.set_irq_ack_callback([s](int level) { s->vdp.acknowledge_irq(level); });
        s->vdp.set_vblank_callback([s](bool in_vblank) {
            s->z80.set_irq_line(in_vblank);
            if (in_vblank) {
                ++s->frame_index;
            }
        });
        // Genesis quirk: the bus controller drops the TAS write phase on a
        // memory operand. Empty callback = drop, preserving flag side-effects.
        s->cpu.set_tas_callback([](std::uint32_t /*addr*/) {});

        // Z80 sound bus ($0000-$FFFF, little-endian).
        // $0000-$3FFF: Z80 RAM (8 KiB, mirrored).
        s->z80_bus.map_mmio(
            0x0000U, 0x4000U, [s](std::uint32_t a) { return s->z80_ram[a & 0x1FFFU]; },
            [s](std::uint32_t a, std::uint8_t v) { s->z80_ram[a & 0x1FFFU] = v; }, 0);
        // $4000-$5FFF: YM2612 (shared with the 68K's $A04000).
        s->z80_bus.map_mmio(
            0x4000U, 0x2000U, [s](std::uint32_t /*a*/) { return s->fm.read_status(); },
            [s](std::uint32_t a, std::uint8_t v) {
                s->fm.write(static_cast<int>((a >> 1U) & 1U), (a & 1U) != 0U, v);
            },
            0);
        // $6000-$60FF: bank register, shifting bit 0 into bit 8 of the 9-bit
        // window that addresses Z80 $8000-$FFFF.
        s->z80_bus.map_mmio(
            0x6000U, 0x100U, [](std::uint32_t /*a*/) -> std::uint8_t { return 0xFFU; },
            [s](std::uint32_t /*a*/, std::uint8_t v) {
                s->z80_bank =
                    static_cast<std::uint16_t>(((s->z80_bank >> 1U) | ((v & 1U) << 8U)) & 0x1FFU);
            },
            0);
        // $7F00-$7FFF: SN76489 PSG at $7F11 (shared with the 68K's $C00011).
        s->z80_bus.map_mmio(
            0x7F00U, 0x100U, [](std::uint32_t /*a*/) -> std::uint8_t { return 0xFFU; },
            [s](std::uint32_t a, std::uint8_t v) {
                if ((a & 0xFFU) == 0x11U) {
                    s->psg.write(v);
                }
            },
            0);
        // $8000-$FFFF: banked 32 KiB window into 68K address space.
        s->z80_bus.map_mmio(
            0x8000U, 0x8000U,
            [s](std::uint32_t a) -> std::uint8_t {
                const std::uint32_t addr =
                    ((static_cast<std::uint32_t>(s->z80_bank) << 15U) | (a & 0x7FFFU)) & 0xFFFFFFU;
                return s->bus.read8(addr);
            },
            [s](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t addr =
                    ((static_cast<std::uint32_t>(s->z80_bank) << 15U) | (a & 0x7FFFU)) & 0xFFFFFFU;
                s->bus.write8(addr, v);
            },
            0);
        s->z80.attach_bus(s->z80_bus);
        s->z80.reset(chips::reset_kind::power_on); // held until the 68K releases RESET

        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on); // loads SSP/PC from the ROM vectors

        return sys;
    }

} // namespace mnemos::manifests::genesis
