#include "c64_system.hpp"

#include <span>
#include <utility>

namespace mnemos::manifests::c64 {

    using region = chips::mapper::c64_pla::region;

    std::unique_ptr<c64_system> assemble_c64(std::vector<std::uint8_t> basic_rom,
                                             std::vector<std::uint8_t> kernal_rom,
                                             std::vector<std::uint8_t> chargen_rom,
                                             const c64_config& config) {
        auto sys = std::make_unique<c64_system>();
        c64_system* s = sys.get();
        s->basic_rom = std::move(basic_rom);
        s->kernal_rom = std::move(kernal_rom);
        s->chargen_rom = std::move(chargen_rom);

        // Region: VIC geometry, phi2 rate, and the mains TOD frequency.
        const bool ntsc = config.video_region == c64_config::region::ntsc;
        s->vic.set_revision(ntsc ? chips::video::vic_ii_6569::revision::ntsc_6567r8
                                 : chips::video::vic_ii_6569::revision::pal_6569);
        const std::uint32_t phi2_hz = ntsc ? 1'022'727U : 985'248U;
        const std::uint32_t tod_hz = ntsc ? 60U : 50U;

        // SID variant + sample rate (both SIDs share the region clock).
        s->sid.set_variant(config.sid_variant);
        s->sid.set_sample_rate(static_cast<std::int32_t>(phi2_hz));
        s->sid2.set_variant(config.sid_variant);
        s->sid2.set_sample_rate(static_cast<std::int32_t>(phi2_hz));

        // The VIC fetches glyphs/screen from main RAM + the character ROM, and
        // colours from colour RAM.
        s->vic.attach_memory({.ram = std::span<const std::uint8_t>(s->ram),
                              .char_rom = std::span<const std::uint8_t>(s->chargen_rom),
                              .color_ram = std::span<const std::uint8_t>(s->color_ram)});
        s->vic.set_bank(0U); // CIA2 port A floats to $00 -> bank 0 until the OS sets it

        // Interrupt wiring. The 6510 /IRQ line is the OR of the VIC raster IRQ and
        // the CIA1 timer/flag IRQ; CIA2 drives /NMI. Each source pushes the live
        // combined level so the machine is fully wired without a per-cycle poll.
        const auto refresh_irq = [s]() {
            s->cpu.set_irq_line(s->vic.irq_asserted() || s->cia1.irq_asserted());
        };
        s->vic.set_irq_callback([refresh_irq](bool) { refresh_irq(); });

        // φ2 + mains TOD source for both CIAs (region-dependent).
        chips::bus_controller::cia_6526::config cia1_cfg;
        cia1_cfg.tod_tick_hz = phi2_hz;
        cia1_cfg.tod_src_hz = tod_hz;
        cia1_cfg.irq_edge = [refresh_irq](bool) { refresh_irq(); };
        // Keyboard + joystick 2 on port A (columns / joy2), keyboard + joystick 1
        // on port B (rows / joy1), resolved against the live driven strobes.
        cia1_cfg.read_port_a = [s]() { return s->input.read_columns(s->cia1.port_b_output()); };
        cia1_cfg.read_port_b = [s]() { return s->input.read_rows(s->cia1.port_a_output()); };
        // Paddle/POT mux: CIA1 PRA bit 7 routes control-port 1's paddles to the SID
        // POTX/POTY, bit 6 routes control-port 2's.
        cia1_cfg.write_port_a = [s](std::uint8_t) {
            const std::uint8_t mux = static_cast<std::uint8_t>(s->cia1.port_a_output() & 0xC0U);
            if (mux == 0x80U) {
                s->sid.set_paddle_x(s->input.paddle_x(1U));
                s->sid.set_paddle_y(s->input.paddle_y(1U));
            } else if (mux == 0x40U) {
                s->sid.set_paddle_x(s->input.paddle_x(2U));
                s->sid.set_paddle_y(s->input.paddle_y(2U));
            }
        };
        s->cia1.configure(cia1_cfg);

        // Datasette: motor from $01 bit 5 (low = on), pulses CIA1 /FLAG, and drives
        // the cassette-sense on $01 bit 4 (low while a key is held).
        chips::storage::datasette::config tape_cfg;
        tape_cfg.motor_on = [s]() { return (s->cpu.read(0x0001U) & 0x20U) == 0U; };
        tape_cfg.flag_pulse = [s]() { s->cia1.flag_edge(); };
        tape_cfg.set_sense = [s](bool held) {
            s->cpu.set_port_input(held ? 0xEFU : 0xFFU); // bit 4 low = key held
        };
        s->tape.configure(tape_cfg);

        // Both drive 8 implementations share the IEC bus; the runner ticks one.
        s->drive8.attach_bus(s->iec);
        s->drive8_full.attach_bus(s->iec);

        chips::bus_controller::cia_6526::config cia2_cfg;
        cia2_cfg.tod_tick_hz = phi2_hz;
        cia2_cfg.tod_src_hz = tod_hz;
        cia2_cfg.irq_edge = [s](bool asserted) { s->cpu.set_nmi_line(asserted); };
        // CIA2 port A: bits 0-1 (inverted) select the VIC's 16K bank; bits 3/4/5
        // drive the IEC ATN/CLK/DATA out lines (a driven 1 pulls the line low, via
        // the 7406 inverter). The callback receives the composed pin level.
        cia2_cfg.write_port_a = [s](std::uint8_t) {
            using line = chips::iec_bus::line;
            // Bank from the composed pins (input bits pull high -> bank 0 at idle).
            s->vic.set_bank(static_cast<std::uint8_t>((~s->cia2.port_a_pins()) & 0x03U));
            // IEC out from the actively driven bits only, so floating pins do not
            // pull the bus (a driven 1 pulls the line low via the 7406 inverter).
            const std::uint8_t out = s->cia2.port_a_output();
            s->iec.set_driver(0U, line::atn, (out & 0x08U) != 0U);
            s->iec.set_driver(0U, line::clk, (out & 0x10U) != 0U);
            s->iec.set_driver(0U, line::data, (out & 0x20U) != 0U);
            // Userport RS-232 TXD is PA2 (high = mark/idle); feed the UART the pin.
            s->rs232_unit.set_txd((s->cia2.port_a_pins() & 0x04U) != 0U);
        };
        // CIA2 port A bits 6/7 read the IEC CLK/DATA in lines (set = released).
        cia2_cfg.read_port_a = [s]() -> std::uint8_t {
            using line = chips::iec_bus::line;
            std::uint8_t value = 0xFFU;
            if (s->iec.asserted(line::clk)) {
                value = static_cast<std::uint8_t>(value & ~0x40U);
            }
            if (s->iec.asserted(line::data)) {
                value = static_cast<std::uint8_t>(value & ~0x80U);
            }
            return value;
        };
        // CIA2 port B is the userport: bit 0 reads the RS-232 RXD line (driven by
        // the UART; mark/idle = high). The remaining userport inputs idle high.
        cia2_cfg.read_port_b = [s]() -> std::uint8_t {
            std::uint8_t value = 0xFFU;
            if (!s->rs232_unit.rxd()) { // space pulls RXD (PB0) low
                value = static_cast<std::uint8_t>(value & ~0x01U);
            }
            return value;
        };
        s->cia2.configure(cia2_cfg);

        // Userport RS-232 modem wiring. The UART bridges the CIA2 serial pins to
        // the byte-level modem: captured TXD bytes drive the modem's DTE input,
        // queued DCE bytes are shifted back out on RXD, and each RXD start bit
        // pulses CIA2 /FLAG. The baud (cycles per bit) follows the configured rate;
        // matching the rate the KERNAL programs into a CIA timer is ROM-gated.
        {
            const std::uint32_t baud = config.rs232_baud == 0U ? 1200U : config.rs232_baud;
            s->rs232_unit.set_cycles_per_bit(phi2_hz / baud);
            s->modem_unit.set_guard_divider(phi2_hz / tod_hz);
            s->rs232_unit.set_byte_sink([s](std::uint8_t b) { s->modem_unit.dte_write(b); });
            s->rs232_unit.set_byte_source(
                [s](std::uint8_t& out) { return s->modem_unit.dte_read(out); });
            s->rs232_unit.set_flag_sink([s]() { s->cia2.flag_edge(); });
        }

        // The PLA decode reads the live 6510 $01 port and the live cartridge /GAME
        // and /EXROM lines each access (no cart -> both float high -> PLA defaults).
        auto decode = [s](std::uint32_t address) {
            const std::uint8_t port = s->cpu.read(0x0001U);
            s->pla.set_cpu_port((port & 0x01U) != 0U, (port & 0x02U) != 0U, (port & 0x04U) != 0U);
            s->pla.set_cart_lines(s->cart.game(), s->cart.exrom());
            return s->pla.decode_cpu_address(static_cast<std::uint16_t>(address));
        };
        auto rom_overlay = [decode](region target) {
            return [decode, target](std::uint32_t address, bool is_write) {
                return !is_write && decode(address) == target; // ROM shadows reads only
            };
        };
        auto io_active = [decode](std::uint32_t address, bool) {
            return decode(address) == region::io;
        };
        // Cartridge ROML/ROMH read-overlays (write-through to RAM), gated by the PLA.
        auto cart_overlay = [decode, s](region target) {
            return [decode, s, target](std::uint32_t address, bool is_write) {
                return !is_write && s->cart.inserted() && decode(address) == target;
            };
        };

        // Base RAM under everything.
        s->bus.map_ram(0x0000U, std::span<std::uint8_t>(s->ram), 0);

        // ROM read-overlays gated by the PLA.
        s->bus.map_rom(0xA000U, std::span<const std::uint8_t>(s->basic_rom), 1,
                       rom_overlay(region::basic));
        s->bus.map_rom(0xE000U, std::span<const std::uint8_t>(s->kernal_rom), 1,
                       rom_overlay(region::kernal));
        s->bus.map_rom(0xD000U, std::span<const std::uint8_t>(s->chargen_rom), 1,
                       rom_overlay(region::chargen));

        // Cartridge ROML ($8000) and ROMH ($A000 in 16K, $E000 in ultimax) banked
        // through the cartridge, plus its I/O-1/I/O-2 window at $DE00-$DFFF.
        s->bus.map_mmio(
            0x8000U, 0x2000U,
            [s](std::uint32_t a) {
                return s->cart.read_roml(static_cast<std::uint16_t>(a - 0x8000U));
            },
            [](std::uint32_t, std::uint8_t) {}, 1, cart_overlay(region::roml));
        s->bus.map_mmio(
            0xA000U, 0x2000U,
            [s](std::uint32_t a) {
                return s->cart.read_romh(static_cast<std::uint16_t>(a - 0xA000U));
            },
            [](std::uint32_t, std::uint8_t) {}, 2, cart_overlay(region::romh));
        s->bus.map_mmio(
            0xE000U, 0x2000U,
            [s](std::uint32_t a) {
                return s->cart.read_romh(static_cast<std::uint16_t>(a - 0xE000U));
            },
            [](std::uint32_t, std::uint8_t) {}, 2, cart_overlay(region::romh));
        s->bus.map_mmio(
            0xDE00U, 0x200U,
            [s](std::uint32_t a) {
                return s->cart.mmio_read(static_cast<std::uint16_t>(a - 0xDE00U));
            },
            [s](std::uint32_t a, std::uint8_t v) {
                s->cart.mmio_write(static_cast<std::uint16_t>(a - 0xDE00U), v);
            },
            2,
            [decode, s](std::uint32_t address, bool) {
                return s->cart.inserted() && decode(address) == region::io;
            });

        // RAM Expansion Unit at I/O-2 ($DF00-$DFFF), above the cartridge window.
        s->reu_unit.attach_bus(s->bus);
        if (config.reu) {
            s->reu_unit.set_model(config.reu_model);
            s->bus.map_mmio(
                0xDF00U, 0x100U,
                [s](std::uint32_t a) {
                    return s->reu_unit.mmio_read(static_cast<std::uint16_t>(a - 0xDF00U));
                },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->reu_unit.mmio_write(static_cast<std::uint16_t>(a - 0xDF00U), v);
                },
                3, [decode](std::uint32_t address, bool) { return decode(address) == region::io; });
        }

        // Open I/O-1/I/O-2 ($DE00-$DFFF): with no cartridge or REU driving these
        // expansion-port lines, the data bus floats and a read returns the last byte
        // the VIC-II fetched (the stale value fastloaders/copy-protection probe),
        // not a clean $FF or the PLA-deselected RAM underneath. Priority 1 sits
        // above base RAM but below the cartridge (2) and REU (3); the no-op write
        // keeps the underlying RAM from being clobbered through the I/O window.
        s->bus.map_mmio(
            0xDE00U, 0x200U, [s](std::uint32_t) { return s->vic.last_fetched_byte(); },
            [](std::uint32_t, std::uint8_t) {}, 1, io_active);

        // I/O space ($D000-$DFFF) — active only when the PLA selects I/O.
        s->bus.map_mmio(
            0xD000U, 0x400U,
            [s](std::uint32_t a) {
                return s->vic.mmio_read(static_cast<std::uint16_t>(a - 0xD000U));
            },
            [s](std::uint32_t a, std::uint8_t v) {
                s->vic.mmio_write(static_cast<std::uint16_t>(a - 0xD000U), v);
            },
            2, io_active);
        s->bus.map_mmio(
            0xD400U, 0x400U,
            [s](std::uint32_t a) {
                return s->sid.mmio_read(static_cast<std::uint16_t>(a - 0xD400U));
            },
            [s](std::uint32_t a, std::uint8_t v) {
                s->sid.mmio_write(static_cast<std::uint16_t>(a - 0xD400U), v);
            },
            2, io_active);
        // Optional second SID at $D420 (stereo): a higher-priority 32-byte overlay so
        // $D420-$D43F routes to SID 2 while the rest of the window mirrors SID 1.
        if (config.dual_sid) {
            s->bus.map_mmio(
                0xD420U, 0x20U,
                [s](std::uint32_t a) {
                    return s->sid2.mmio_read(static_cast<std::uint16_t>(a - 0xD420U));
                },
                [s](std::uint32_t a, std::uint8_t v) {
                    s->sid2.mmio_write(static_cast<std::uint16_t>(a - 0xD420U), v);
                },
                3, io_active);
        }
        s->bus.map_ram(0xD800U, std::span<std::uint8_t>(s->color_ram), 2, io_active); // colour RAM
        s->bus.map_mmio(
            0xDC00U, 0x100U,
            [s](std::uint32_t a) {
                return s->cia1.mmio_read(static_cast<std::uint16_t>(a - 0xDC00U));
            },
            [s](std::uint32_t a, std::uint8_t v) {
                s->cia1.mmio_write(static_cast<std::uint16_t>(a - 0xDC00U), v);
            },
            2, io_active);
        s->bus.map_mmio(
            0xDD00U, 0x100U,
            [s](std::uint32_t a) {
                return s->cia2.mmio_read(static_cast<std::uint16_t>(a - 0xDD00U));
            },
            [s](std::uint32_t a, std::uint8_t v) {
                s->cia2.mmio_write(static_cast<std::uint16_t>(a - 0xDD00U), v);
            },
            2, io_active);

        s->cpu.attach_bus(s->bus);
        return sys;
    }

} // namespace mnemos::manifests::c64
