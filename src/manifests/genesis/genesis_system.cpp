#include "genesis_system.hpp"

#include <span>
#include <utility>

namespace mnemos::manifests::genesis {

    std::unique_ptr<genesis_system> assemble_genesis(std::vector<std::uint8_t> rom,
                                                     const genesis_config& config) {
        auto sys = std::make_unique<genesis_system>();
        genesis_system* s = sys.get();
        s->rom = std::move(rom);

        const bool pal = config.video_region == genesis_config::region::pal;
        s->vdp.set_pal(pal);

        // Region/version readback ($A10001): bit7 = overseas (export), bit6 = PAL,
        // bit5 = no expansion unit. We model an export console.
        s->version_register = static_cast<std::uint8_t>(0x80U | (pal ? 0x40U : 0x00U) | 0x20U);

        // --- $000000-$3FFFFF: cartridge ROM (read-only). ---
        s->bus.map_rom(0x000000U, std::span<const std::uint8_t>(s->rom), 0);

        // --- $A00000-$A03FFF: Z80 RAM (8 KiB, mirrored). ---
        s->bus.map_mmio(
            0xA00000U, 0x4000U, [s](std::uint32_t a) { return s->z80_ram[a & 0x1FFFU]; },
            [s](std::uint32_t a, std::uint8_t v) { s->z80_ram[a & 0x1FFFU] = v; }, 0);

        // --- $A04000-$A05FFF: YM2612 (4 byte registers, mirrored). ---
        // $A04000 port0 addr, $A04001 port0 data, $A04002 port1 addr, $A04003 port1 data.
        s->bus.map_mmio(
            0xA04000U, 0x2000U, [s](std::uint32_t /*a*/) { return s->fm.read_status(); },
            [s](std::uint32_t a, std::uint8_t v) {
                const int port = static_cast<int>((a >> 1U) & 1U);
                const bool data = (a & 1U) != 0U;
                s->fm.write(port, data, v);
            },
            0);

        // --- $A10000-$A1001F: controller I/O + version register. ---
        s->bus.map_mmio(
            0xA10000U, 0x20U,
            [s](std::uint32_t a) -> std::uint8_t {
                switch (a & 0x1FU) {
                case 0x01:
                    return s->version_register;
                case 0x03: // controller 1 data (idle: nothing pressed)
                case 0x05: // controller 2 data
                    return 0x7FU;
                default:
                    return 0xFFU;
                }
            },
            [](std::uint32_t /*a*/, std::uint8_t /*v*/) {
                // Controller direction/data latches: full 3/6-button protocol is the
                // next phase; writes are accepted and dropped for now.
            },
            0);

        // --- $A11100 Z80 BUSREQ: the 68000 requests the Z80 bus to share the A-bank. ---
        // A read reports bit0 = 1 while the Z80 still holds its bus (request not yet
        // granted); games spin until it clears. The relevant bit is in the high byte
        // (even address) of the word access.
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

        // --- $A11200 Z80 RESET: bit0 0 = held in reset, 1 = released. ---
        s->bus.map_mmio(
            0xA11200U, 0x2U, [](std::uint32_t /*a*/) -> std::uint8_t { return 0x00U; },
            [s](std::uint32_t a, std::uint8_t v) {
                if ((a & 1U) != 0U) {
                    return;
                }
                const bool released = (v & 0x01U) != 0U;
                if (!released && s->z80_reset_released) {
                    s->z80.reset(
                        chips::reset_kind::power_on); // falling edge holds the Z80 at reset
                }
                s->z80_reset_released = released;
                s->z80_running = s->z80_reset_released && !s->z80_bus_requested;
            },
            0);

        // --- $C00000-$C0001F: VDP ports (16-bit) + the SN76489 PSG at $C00011. ---
        // The 68000 word access splits into an even byte (high) then an odd byte (low);
        // we coalesce them so the VDP's two-word command protocol sees whole words.
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
                    s->psg.write(v); // PSG data port
                    return;
                }
                if ((offset & 1U) == 0U) {
                    s->vdp_write_high = v; // latch the high byte
                } else {
                    s->vdp.write16(offset & ~1U,
                                   static_cast<std::uint16_t>(
                                       (static_cast<std::uint16_t>(s->vdp_write_high) << 8U) | v));
                }
            },
            0);

        // --- $E00000-$FFFFFF: 68K work RAM (64 KiB, mirrored every 64 KiB). ---
        s->bus.map_mmio(
            0xE00000U, 0x200000U, [s](std::uint32_t a) { return s->work_ram[a & 0xFFFFU]; },
            [s](std::uint32_t a, std::uint8_t v) { s->work_ram[a & 0xFFFFU] = v; }, 0);

        // --- VDP DMA reads a big-endian word from 68K address space. ---
        s->vdp.set_dma_read([s](std::uint32_t addr) -> std::uint16_t {
            const auto hi = s->bus.read8(addr & 0xFFFFFFU);
            const auto lo = s->bus.read8((addr + 1U) & 0xFFFFFFU);
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8U) | lo);
        });

        // --- VDP V/H-blank interrupt drives the 68000 IPL pins. ---
        s->vdp.set_irq_callback([s](int level) { s->cpu.set_irq_level(level); });
        // The 68000 IACK cycle clears the VDP's interrupt request (many games' V-blank
        // handlers rely on this rather than reading the VDP status to ack).
        s->cpu.set_irq_ack_callback([s](int level) { s->vdp.acknowledge_irq(level); });

        // --- Z80 sound bus ($0000-$FFFF, little-endian). ---
        // $0000-$3FFF: Z80 RAM (8 KiB, mirrored).
        s->z80_bus.map_mmio(
            0x0000U, 0x4000U, [s](std::uint32_t a) { return s->z80_ram[a & 0x1FFFU]; },
            [s](std::uint32_t a, std::uint8_t v) { s->z80_ram[a & 0x1FFFU] = v; }, 0);
        // $4000-$5FFF: YM2612 (the same chip the 68000 sees at $A04000).
        s->z80_bus.map_mmio(
            0x4000U, 0x2000U, [s](std::uint32_t /*a*/) { return s->fm.read_status(); },
            [s](std::uint32_t a, std::uint8_t v) {
                s->fm.write(static_cast<int>((a >> 1U) & 1U), (a & 1U) != 0U, v);
            },
            0);
        // $6000-$60FF: bank register. Each write shifts its bit 0 into bit 8 of the
        // 9-bit bank that addresses the $8000 window.
        s->z80_bus.map_mmio(
            0x6000U, 0x100U, [](std::uint32_t /*a*/) -> std::uint8_t { return 0xFFU; },
            [s](std::uint32_t /*a*/, std::uint8_t v) {
                s->z80_bank =
                    static_cast<std::uint16_t>(((s->z80_bank >> 1U) | ((v & 1U) << 8U)) & 0x1FFU);
            },
            0);
        // $7F00-$7FFF: the SN76489 PSG at $7F11 (same chip as the 68000's $C00011).
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
        s->z80.reset(chips::reset_kind::power_on); // held here until the 68000 releases RESET

        // --- CPU bus + boot from the ROM reset vectors. ---
        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on); // now reads SSP/PC from the ROM vectors

        return sys;
    }

} // namespace mnemos::manifests::genesis
