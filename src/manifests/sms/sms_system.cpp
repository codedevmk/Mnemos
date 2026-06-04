#include "sms_system.hpp"

#include "mk3020.hpp" // default controller-port peripheral (Master System Control Pad)

#include <memory>
#include <span>
#include <utility>

namespace mnemos::manifests::sms {

    // A Codemasters cart carries its own checksum header (the Sega BIOS routine
    // can't validate it through the different mapper): the 16-bit word at $7FE6
    // plus the complement word at $7FE8 sum to $10000. That doubles as the mapper
    // signature (community-documented header convention; see THIRD-PARTY.md).
    bool detect_codemasters(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() < 0x8000U) {
            return false;
        }
        const auto word = [&](std::size_t off) {
            return static_cast<std::uint32_t>(rom[off]) |
                   (static_cast<std::uint32_t>(rom[off + 1U]) << 8U);
        };
        return (word(0x7FE6U) + word(0x7FE8U)) == 0x10000U;
    }

    sms_config::mapper resolve_mapper(const sms_config& config,
                                      std::span<const std::uint8_t> rom) noexcept {
        switch (config.cartridge_mapper) {
        case sms_config::mapper::sega:
            return sms_config::mapper::sega;
        case sms_config::mapper::codemasters:
            return sms_config::mapper::codemasters;
        case sms_config::mapper::korean:
            return sms_config::mapper::korean;
        case sms_config::mapper::korean_msx:
            return sms_config::mapper::korean_msx;
        case sms_config::mapper::korean_msx_nemesis:
            return sms_config::mapper::korean_msx_nemesis;
        case sms_config::mapper::korean_hicom:
            return sms_config::mapper::korean_hicom;
        case sms_config::mapper::automatic:
        default:
            return detect_codemasters(rom) ? sms_config::mapper::codemasters
                                           : sms_config::mapper::sega;
        }
    }

    namespace {
        // I/O control latch ($3F) bit layout: 0/1 = port-0 TR/TH direction
        // (1 = input), 2/3 = port-1, 4/5 = port-0 TR/TH output level, 6/7 =
        // port-1 output level.
        bool tr_is_input(const sms_system* s, int port) noexcept {
            return (s->io_ctrl & (1U << static_cast<unsigned>(port * 2))) != 0U;
        }
        bool th_is_input(const sms_system* s, int port) noexcept {
            return (s->io_ctrl & (1U << static_cast<unsigned>(port * 2 + 1))) != 0U;
        }
        std::uint8_t tr_output(const sms_system* s, int port) noexcept {
            return static_cast<std::uint8_t>((s->io_ctrl >> static_cast<unsigned>(4 + port * 2)) &
                                             1U);
        }
        std::uint8_t th_output(const sms_system* s, int port) noexcept {
            return static_cast<std::uint8_t>((s->io_ctrl >> static_cast<unsigned>(5 + port * 2)) &
                                             1U);
        }

        // Active-low input byte from an attached peripheral (or 0xFF if the
        // socket is empty -- pull-up = idle high).
        std::uint8_t port_input(const sms_system* s, int port) noexcept {
            const auto& dev = s->ports[static_cast<std::size_t>(port)];
            return dev ? dev->read_data() : 0xFFU;
        }

        // $DC byte: P1's 6 input pins in bits 0..5, P2's Up/Down in bits 6..7.
        // The pad's Button 2 line at bit 5 is overridden by tr_output when TR
        // is configured as a host-driven output for that port.
        std::uint8_t read_pad_dc(const sms_system* s) noexcept {
            const std::uint8_t p1 = port_input(s, 0);
            const std::uint8_t p2 = port_input(s, 1);
            std::uint8_t v = static_cast<std::uint8_t>(p1 & 0x1FU); // U/D/L/R/B1
            const std::uint8_t b2 =
                tr_is_input(s, 0) ? static_cast<std::uint8_t>((p1 >> 5U) & 1U) : tr_output(s, 0);
            v |= static_cast<std::uint8_t>(b2 << 5U);
            v |= static_cast<std::uint8_t>(((p2 >> 0U) & 1U) << 6U); // P2 Up
            v |= static_cast<std::uint8_t>(((p2 >> 1U) & 1U) << 7U); // P2 Down
            return v;
        }

        // $DD byte: P2's L/R/B1/B2 in bits 0..3, reset at bit 4, bit 5 idles
        // high, bits 6/7 carry the TH pin levels (pad input or driven output).
        std::uint8_t read_pad_dd(const sms_system* s) noexcept {
            const std::uint8_t p2 = port_input(s, 1);
            std::uint8_t v = 0U;
            v |= static_cast<std::uint8_t>(((p2 >> 2U) & 1U) << 0U); // Left
            v |= static_cast<std::uint8_t>(((p2 >> 3U) & 1U) << 1U); // Right
            v |= static_cast<std::uint8_t>(((p2 >> 4U) & 1U) << 2U); // Button 1
            const std::uint8_t b2 =
                tr_is_input(s, 1) ? static_cast<std::uint8_t>((p2 >> 5U) & 1U) : tr_output(s, 1);
            v |= static_cast<std::uint8_t>(b2 << 3U);
            v |= static_cast<std::uint8_t>((s->reset_pressed ? 0U : 1U) << 4U);
            v |= static_cast<std::uint8_t>(1U << 5U);
            v |= static_cast<std::uint8_t>((th_is_input(s, 0) ? 1U : th_output(s, 0)) << 6U);
            v |= static_cast<std::uint8_t>((th_is_input(s, 1) ? 1U : th_output(s, 1)) << 7U);
            return v;
        }
    } // namespace

    std::unique_ptr<sms_system> assemble_sms(std::vector<std::uint8_t> rom,
                                             const sms_config& config) {
        auto sys = std::make_unique<sms_system>();
        sms_system* s = sys.get();
        s->rom = std::move(rom);

        // Region: NTSC = 262 scanlines, PAL = 313.
        s->vdp.set_pal(config.video_region == mnemos::video_region::pal);

        // Pick the cartridge mapper: forced by config, otherwise auto-detected from
        // the cart's Codemasters checksum header (Korean is force-only).
        const sms_config::mapper kind =
            resolve_mapper(config, std::span<const std::uint8_t>(s->rom));
        s->codemasters_active = kind == sms_config::mapper::codemasters;
        s->korean_active = kind == sms_config::mapper::korean;
        s->korean_msx_active = kind == sms_config::mapper::korean_msx ||
                               kind == sms_config::mapper::korean_msx_nemesis;
        s->korean_hicom_active = kind == sms_config::mapper::korean_hicom;

        // --- Z80 memory map (16-bit address space) ---
        // $C000-$DFFF: 8 KiB system RAM, mirrored at $E000-$FFFF (the same storage).
        const std::span<std::uint8_t> work_ram(s->ram);

        if (s->korean_msx_active) {
            // Korean MSX: $0000-$BFFF routed through the 8 KiB mapper. $0000-$3FFF is
            // fixed bank 0; writes to $0000-$0003 page the four 8 KiB windows. The
            // Nemesis variant remaps $0000-$1FFF to the last 8 KiB bank.
            if (kind == sms_config::mapper::korean_msx_nemesis) {
                s->korean_msx.set_variant(chips::mapper::korean_msx_mapper::variant::nemesis);
            }
            s->korean_msx.attach_rom(std::span<const std::uint8_t>(s->rom));
            s->bus.map_mmio(
                0x0000U, 0xC000U,
                [s](std::uint32_t a) {
                    return s->korean_msx.cpu_read(static_cast<std::uint16_t>(a));
                },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->korean_msx.cpu_write(static_cast<std::uint16_t>(a), v);
                },
                0);
            s->bus.map_ram(0xC000U, work_ram, 0);
            s->bus.map_ram(0xE000U, work_ram, 0);
        } else if (s->korean_active) {
            // Korean: $0000-$BFFF routed through the Korean mapper. Slots 0/1 are
            // fixed ROM banks 0/1; a write to $A000 (inside the window) pages slot 2.
            // No $FFFC-$FFFF register overlay -- $C000-$FFFF is plain work RAM.
            s->korean.attach_rom(std::span<const std::uint8_t>(s->rom));
            s->bus.map_mmio(
                0x0000U, 0xC000U,
                [s](std::uint32_t a) { return s->korean.cpu_read(static_cast<std::uint16_t>(a)); },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->korean.cpu_write(static_cast<std::uint16_t>(a), v);
                },
                0);
            s->bus.map_ram(0xC000U, work_ram, 0);
            s->bus.map_ram(0xE000U, work_ram, 0);
        } else if (s->codemasters_active) {
            // Codemasters: $0000-$BFFF is three ROM slots plus the $A000-$BFFF cart-RAM
            // window. The page registers live in ROM space, so writes route to the
            // mapper as well (there is no $FFFC-$FFFF register overlay).
            s->codies.attach_rom(std::span<const std::uint8_t>(s->rom));
            s->bus.map_mmio(
                0x0000U, 0xC000U,
                [s](std::uint32_t a) { return s->codies.cpu_read(static_cast<std::uint16_t>(a)); },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->codies.cpu_write(static_cast<std::uint16_t>(a), v);
                },
                0);
            s->bus.map_ram(0xC000U, work_ram, 0);
            s->bus.map_ram(0xE000U, work_ram, 0);
        } else if (s->korean_hicom_active) {
            // HiCom 188-in-1: $0000-$BFFF follows a single 32 KiB page register.
            // $0000-$7FFF is the page; $8000-$BFFF mirrors its lower 16 KiB. The
            // register sits at $FFFF in the work-RAM mirror, so -- like the Sega
            // mapper's $FFFC-$FFFF -- a write there lands in RAM AND updates the
            // mapper; reads return the RAM byte. Priority 1 wins over RAM.
            s->hicom.attach_rom(std::span<const std::uint8_t>(s->rom));
            s->bus.map_mmio(
                0x0000U, 0xC000U,
                [s](std::uint32_t a) { return s->hicom.cpu_read(static_cast<std::uint16_t>(a)); },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->hicom.cpu_write(static_cast<std::uint16_t>(a), v);
                },
                0);
            s->bus.map_ram(0xC000U, work_ram, 0);
            s->bus.map_ram(0xE000U, work_ram, 0);
            s->bus.map_mmio(
                chips::mapper::hicom_mapper::bank_register, 0x1U,
                [s](std::uint32_t a) { return s->ram[a & 0x1FFFU]; },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->ram[a & 0x1FFFU] = v;
                    s->hicom.cpu_write(static_cast<std::uint16_t>(a), v);
                },
                1);
        } else {
            // Sega: $0000-$BFFF is the banked ROM / optional cart RAM via the mapper.
            s->mapper.attach_rom(std::span<const std::uint8_t>(s->rom));
            s->bus.map_mmio(
                0x0000U, 0xC000U,
                [s](std::uint32_t a) { return s->mapper.cpu_read(static_cast<std::uint16_t>(a)); },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->mapper.cpu_write(static_cast<std::uint16_t>(a), v);
                },
                0);
            s->bus.map_ram(0xC000U, work_ram, 0);
            s->bus.map_ram(0xE000U, work_ram, 0);
            // The mapper control registers overlap the top of the RAM mirror: a write
            // both lands in RAM (games read $FFFC-$FFFF back as ordinary work RAM) and
            // updates the mapper; reads return the RAM byte. Priority 1 wins over RAM.
            s->bus.map_mmio(
                chips::mapper::sms_mapper::register_base, 0x4U,
                [s](std::uint32_t a) { return s->ram[a & 0x1FFFU]; },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->ram[a & 0x1FFFU] = v;
                    s->mapper.write_register(static_cast<std::uint16_t>(a), v);
                },
                1);
        }

        // --- Z80 I/O ports (separate 64K IN/OUT space) ---
        s->cpu.set_port_in([s](std::uint16_t port) -> std::uint8_t {
            const auto p = static_cast<std::uint8_t>(port & 0xFFU);
            if (p <= 0x3FU) {
                return 0xFFU; // open bus
            }
            if (p <= 0x7FU) {
                return (p & 1U) != 0U ? s->vdp.hcounter() : s->vdp.vcounter();
            }
            if (p <= 0xBFU) {
                return (p & 1U) != 0U ? s->vdp.ctrl_read() : s->vdp.data_read();
            }
            return (p & 1U) != 0U ? read_pad_dd(s) : read_pad_dc(s);
        });
        s->cpu.set_port_out([s](std::uint16_t port, std::uint8_t value) {
            const auto p = static_cast<std::uint8_t>(port & 0xFFU);
            if (p <= 0x3FU) {
                if ((p & 1U) != 0U) {
                    s->io_ctrl = value; // I/O port control ($3F)
                }
                return;
            }
            if (p <= 0x7FU) {
                s->psg.write(value);
                return;
            }
            if (p <= 0xBFU) {
                if ((p & 1U) != 0U) {
                    s->vdp.ctrl_write(value);
                } else {
                    s->vdp.data_write(value);
                }
                return;
            }
            // $C0-$FF: no writable registers on a base SMS.
        });

        // --- Interrupts: the VDP /INT line is ORed into the Z80 IRQ. ---
        s->vdp.set_irq_callback([s](bool) { s->cpu.set_irq_line(s->vdp.irq_asserted()); });

        // --- CPU bus + post-BIOS stack pointer. ---
        s->cpu.attach_bus(s->bus);
        // The Z80 powers on with SP=$FFFF; the SMS BIOS sets SP=$DFF0 before handing
        // off to the cart. We do not boot the BIOS, so emulate its post-init SP here
        // so a cart that issues a CALL before setting SP does not push onto the
        // $FFFC-$FFFF mapper page registers and corrupt its own banking.
        auto regs = s->cpu.cpu_registers();
        regs.sp = 0xDFF0U;
        s->cpu.set_registers(regs);

        // Default-plug an MK-3020 Master System Control Pad into both
        // sockets. Adapters can swap for other input peripherals later
        // (Light Phaser, Sports Pad, Sega Mouse).
        s->attach(0, std::make_unique<peripheral::input::mk3020>());
        s->attach(1, std::make_unique<peripheral::input::mk3020>());

        return sys;
    }

} // namespace mnemos::manifests::sms
