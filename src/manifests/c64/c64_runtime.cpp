#include "c64_runtime.hpp"

#include "c64_manifests.hpp" // manifest_toml
#include "manifest.hpp"      // parse_manifest

#include <cstdio>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace mnemos::manifests::c64 {

    std::unique_ptr<c64_runtime> build_c64_runtime(std::vector<std::uint8_t> basic_rom,
                                                   std::vector<std::uint8_t> kernal_rom,
                                                   std::vector<std::uint8_t> chargen_rom,
                                                   const c64_config& config) {
        auto rt = std::make_unique<c64_runtime>();
        rt->state.cart = &rt->cart; // PLA predicates read the cart /GAME, /EXROM lines

        // Banking predicates close over &rt->state (chip pointers filled below).
        auto preds = make_c64_overlay_predicates(rt->state);

        const auto roms = [&](std::string_view f) -> std::optional<std::vector<std::uint8_t>> {
            if (f == "basic.bin") {
                return basic_rom;
            }
            if (f == "kernal.bin") {
                return kernal_rom;
            }
            if (f == "chargen.bin") {
                return chargen_rom;
            }
            return std::nullopt;
        };

        const auto parsed = parse_manifest(manifest_toml(config.video_region));
        if (!parsed.ok()) {
            for (const auto& d : parsed.errors) {
                std::fprintf(stderr, "[c64-manifest parse] %s\n", d.message.c_str());
            }
            std::fflush(stderr);
            return rt;
        }
        auto built = build_system(*parsed.value, roms, {}, {}, {}, preds);
        if (!built.ok()) {
            for (const auto& d : built.errors) {
                std::fprintf(stderr, "[c64-manifest build] %s\n", d.message.c_str());
            }
            std::fflush(stderr);
            return rt;
        }
        rt->graph = std::move(*built.value);

        // Wire chip pointers from the constructed graph.
        rt->state.cpu = dynamic_cast<chips::cpu::m6510*>(rt->graph.chip("cpu"));
        rt->state.pla = dynamic_cast<chips::mapper::c64_pla*>(rt->graph.chip("pla"));
        rt->vic = dynamic_cast<chips::video::vic_ii_6569*>(rt->graph.chip("video"));
        rt->sid = dynamic_cast<chips::audio::sid_6581*>(rt->graph.chip("audio"));
        rt->cia1 = dynamic_cast<chips::bus_controller::cia_6526*>(rt->graph.chip("cia1"));
        rt->cia2 = dynamic_cast<chips::bus_controller::cia_6526*>(rt->graph.chip("cia2"));

        auto* cpu = rt->state.cpu;
        auto* vic = rt->vic;
        auto* sid = rt->sid;
        auto* cia1 = rt->cia1;
        auto* cia2 = rt->cia2;
        auto* bus = rt->graph.bus("main");
        c64_runtime* s = rt.get();

        // ---- Wiring lifted intact from assemble_c64 (chips from the graph,
        //      memory from region_span; base RAM / ROM overlays / colour RAM /
        //      VIC-SID-CIA MMIO are already mapped by build_system). ----

        const bool ntsc = config.video_region == c64_config::region::ntsc;
        vic->set_revision(ntsc ? chips::video::vic_ii_6569::revision::ntsc_6567r8
                               : chips::video::vic_ii_6569::revision::pal_6569);
        const std::uint32_t phi2_hz = ntsc ? 1'022'727U : 985'248U;
        const std::uint32_t tod_hz = ntsc ? 60U : 50U;

        sid->set_variant(config.sid_variant);
        sid->set_sample_rate(static_cast<std::int32_t>(phi2_hz));
        s->sid2.set_variant(config.sid_variant);
        s->sid2.set_sample_rate(static_cast<std::int32_t>(phi2_hz));

        vic->attach_memory(
            {.ram = std::span<const std::uint8_t>(rt->graph.region_span("ram")),
             .char_rom = std::span<const std::uint8_t>(rt->graph.region_span("char_rom")),
             .color_ram = std::span<const std::uint8_t>(rt->graph.region_span("color_ram"))});
        vic->set_bank(0U);

        const auto refresh_irq = [cpu, vic, cia1]() {
            cpu->set_irq_line(vic->irq_asserted() || cia1->irq_asserted());
        };
        vic->set_irq_callback([refresh_irq](bool) { refresh_irq(); });

        chips::bus_controller::cia_6526::config cia1_cfg;
        cia1_cfg.tod_tick_hz = phi2_hz;
        cia1_cfg.tod_src_hz = tod_hz;
        cia1_cfg.irq_edge = [refresh_irq](bool) { refresh_irq(); };
        cia1_cfg.read_port_a = [s, cia1]() { return s->input.read_columns(cia1->port_b_output()); };
        cia1_cfg.read_port_b = [s, cia1]() { return s->input.read_rows(cia1->port_a_output()); };
        cia1_cfg.write_port_a = [s, sid, cia1](std::uint8_t) {
            const std::uint8_t mux = static_cast<std::uint8_t>(cia1->port_a_output() & 0xC0U);
            if (mux == 0x80U) {
                sid->set_paddle_x(s->input.paddle_x(1U));
                sid->set_paddle_y(s->input.paddle_y(1U));
            } else if (mux == 0x40U) {
                sid->set_paddle_x(s->input.paddle_x(2U));
                sid->set_paddle_y(s->input.paddle_y(2U));
            }
        };
        cia1->configure(cia1_cfg);

        chips::storage::datasette::config tape_cfg;
        tape_cfg.motor_on = [cpu]() { return (cpu->read(0x0001U) & 0x20U) == 0U; };
        tape_cfg.flag_pulse = [cia1]() { cia1->flag_edge(); };
        tape_cfg.set_sense = [cpu](bool held) { cpu->set_port_input(held ? 0xEFU : 0xFFU); };
        s->tape.configure(tape_cfg);

        s->drive8.attach_bus(s->iec);
        s->drive8_full.attach_bus(s->iec);

        chips::bus_controller::cia_6526::config cia2_cfg;
        cia2_cfg.tod_tick_hz = phi2_hz;
        cia2_cfg.tod_src_hz = tod_hz;
        cia2_cfg.irq_edge = [cpu](bool asserted) { cpu->set_nmi_line(asserted); };
        cia2_cfg.write_port_a = [s, vic, cia2](std::uint8_t) {
            using line = chips::iec_bus::line;
            vic->set_bank(static_cast<std::uint8_t>((~cia2->port_a_pins()) & 0x03U));
            const std::uint8_t out = cia2->port_a_output();
            s->iec.set_driver(0U, line::atn, (out & 0x08U) != 0U);
            s->iec.set_driver(0U, line::clk, (out & 0x10U) != 0U);
            s->iec.set_driver(0U, line::data, (out & 0x20U) != 0U);
            s->rs232_unit.set_txd((cia2->port_a_pins() & 0x04U) != 0U);
        };
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
        cia2_cfg.read_port_b = [s]() -> std::uint8_t {
            std::uint8_t value = 0xFFU;
            if (!s->rs232_unit.rxd()) {
                value = static_cast<std::uint8_t>(value & ~0x01U);
            }
            return value;
        };
        cia2->configure(cia2_cfg);

        {
            const std::uint32_t baud = config.rs232_baud == 0U ? 1200U : config.rs232_baud;
            s->rs232_unit.set_cycles_per_bit(phi2_hz / baud);
            s->modem_unit.set_guard_divider(phi2_hz / tod_hz);
            s->rs232_unit.set_byte_sink([s](std::uint8_t b) { s->modem_unit.dte_write(b); });
            s->rs232_unit.set_byte_source(
                [s](std::uint8_t& out) { return s->modem_unit.dte_read(out); });
            s->rs232_unit.set_flag_sink([cia2]() { cia2->flag_edge(); });
        }

        // Open I/O-1/I/O-2 ($DE00-$DFFF): with no cartridge/REU driving the
        // expansion lines, a read returns the last byte the VIC fetched. Priority
        // 1 (above base RAM), active only when the PLA selects I/O. (Cartridge,
        // REU and the second SID stay unmapped in the default config -- they never
        // activate, so omitting them is byte-identical to assemble_c64.)
        auto* pla = rt->state.pla;
        auto io_selected = [cpu, pla, s](std::uint32_t address, bool) {
            const std::uint8_t port = cpu->read(0x0001U);
            pla->set_cpu_port((port & 0x01U) != 0U, (port & 0x02U) != 0U, (port & 0x04U) != 0U);
            pla->set_cart_lines(s->cart.game(), s->cart.exrom());
            return pla->decode_cpu_address(static_cast<std::uint16_t>(address)) ==
                   chips::mapper::c64_pla::region::io;
        };
        bus->map_mmio(
            0xDE00U, 0x200U, [vic](std::uint32_t) { return vic->last_fetched_byte(); },
            [](std::uint32_t, std::uint8_t) {}, 1, io_selected);

        // Reset all chips (matching the golden-boot order). build_system does not
        // reset chips, and the 6510 must load its reset vector from the KERNAL.
        cpu->reset(chips::reset_kind::power_on);
        cia1->reset(chips::reset_kind::power_on);
        cia2->reset(chips::reset_kind::power_on);
        sid->reset(chips::reset_kind::power_on);
        s->sid2.reset(chips::reset_kind::power_on);
        vic->reset(chips::reset_kind::power_on);
        s->drive8.reset(chips::reset_kind::power_on);
        s->tape.reset(chips::reset_kind::power_on);

        return rt;
    }

} // namespace mnemos::manifests::c64
