#include <mnemos/manifests/c64/c64_system.hpp>

#include <span>
#include <utility>

namespace mnemos::manifests::c64 {

    using region = chips::mapper::c64_pla::region;

    std::unique_ptr<c64_system> assemble_c64(std::vector<std::uint8_t> basic_rom,
                                             std::vector<std::uint8_t> kernal_rom,
                                             std::vector<std::uint8_t> chargen_rom) {
        auto sys = std::make_unique<c64_system>();
        c64_system* s = sys.get();
        s->basic_rom = std::move(basic_rom);
        s->kernal_rom = std::move(kernal_rom);
        s->chargen_rom = std::move(chargen_rom);

        s->vic.set_revision(chips::video::vic_ii_6569::revision::pal_6569);

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

        // PAL φ2 with a 50 Hz TOD source for both CIAs.
        chips::bus_controller::cia_6526::config cia1_cfg;
        cia1_cfg.tod_tick_hz = 985'248U;
        cia1_cfg.tod_src_hz = 50U;
        cia1_cfg.irq_edge = [refresh_irq](bool) { refresh_irq(); };
        s->cia1.configure(cia1_cfg);

        // The protocol-level drive shares the IEC bus as device 8.
        s->drive8.attach_bus(s->iec);

        chips::bus_controller::cia_6526::config cia2_cfg;
        cia2_cfg.tod_tick_hz = 985'248U;
        cia2_cfg.tod_src_hz = 50U;
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
        s->cia2.configure(cia2_cfg);

        // The PLA decode reads the live 6510 $01 port each access (bare machine, so
        // /GAME and /EXROM float high — the PLA defaults).
        auto decode = [s](std::uint32_t address) {
            const std::uint8_t port = s->cpu.read(0x0001U);
            s->pla.set_cpu_port((port & 0x01U) != 0U, (port & 0x02U) != 0U, (port & 0x04U) != 0U);
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

        // Base RAM under everything.
        s->bus.map_ram(0x0000U, std::span<std::uint8_t>(s->ram), 0);

        // ROM read-overlays gated by the PLA.
        s->bus.map_rom(0xA000U, std::span<const std::uint8_t>(s->basic_rom), 1,
                       rom_overlay(region::basic));
        s->bus.map_rom(0xE000U, std::span<const std::uint8_t>(s->kernal_rom), 1,
                       rom_overlay(region::kernal));
        s->bus.map_rom(0xD000U, std::span<const std::uint8_t>(s->chargen_rom), 1,
                       rom_overlay(region::chargen));

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
