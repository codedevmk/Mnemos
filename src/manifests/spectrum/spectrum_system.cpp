#include "spectrum_system.hpp"

#include <cstring>

namespace mnemos::manifests::spectrum {

    namespace {
        constexpr std::uint32_t k_state_version = 1U;
    } // namespace

    void spectrum_system::set_key(int row, int bit, bool pressed) noexcept {
        if (row < 0 || row >= 8 || bit < 0 || bit >= 5) {
            return;
        }
        const auto mask = static_cast<std::uint8_t>(1U << bit);
        if (pressed) {
            keyboard_rows[static_cast<std::size_t>(row)] &= static_cast<std::uint8_t>(~mask);
        } else {
            keyboard_rows[static_cast<std::size_t>(row)] |= mask;
        }
    }

    void spectrum_system::apply_snapshot(const spectrum_snapshot& snap) noexcept {
        cpu.set_registers(snap.regs);
        std::memcpy(ram.data(), snap.ram.data(), ram.size());
        ula.set_border(snap.border);
    }

    void spectrum_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.bytes(keyboard_rows);
        writer.boolean(ear_input);
    }

    void spectrum_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        reader.bytes(keyboard_rows);
        ear_input = reader.boolean();
    }

    std::unique_ptr<spectrum_system> assemble_spectrum(std::span<const std::uint8_t> rom,
                                                       const spectrum_config& /*config*/) {
        auto sys = std::make_unique<spectrum_system>();
        spectrum_system* s = sys.get();
        s->keyboard_rows.fill(0xFFU); // all keys released

        const std::size_t n = rom.size() < s->rom.size() ? rom.size() : s->rom.size();
        std::memcpy(s->rom.data(), rom.data(), n);

        // $0000-$3FFF: system ROM (writes dropped). $4000-$FFFF: 48 KiB RAM.
        s->bus.map_rom(0x0000U, std::span<const std::uint8_t>(s->rom), 0);
        s->bus.map_ram(0x4000U, std::span<std::uint8_t>(s->ram), 0);

        // The ULA reads the display out of the screen window ($4000-$5AFF) and
        // raises the Z80 /INT through the injected callback.
        s->ula.set_screen_ram(
            std::span<const std::uint8_t>(s->ram).first(chips::video::ula::screen_ram_bytes));
        s->ula.set_irq_callback([s](bool asserted) { s->cpu.set_irq_line(asserted); });

        // Z80 I/O space. The ULA decodes any port with A0 = 0 (canonically $FE):
        // reads return the keyboard half-rows selected by the high address byte
        // (active-low) with EAR in bit 6; writes set the border (bits 0-2). The
        // MIC/EAR beeper bits and all odd ports are not modelled here.
        s->cpu.set_port_in([s](std::uint16_t port) -> std::uint8_t {
            if ((port & 0x00FFU) == 0x1FU) {
                return s->kempston; // Kempston joystick
            }
            if ((port & 0x0001U) != 0U) {
                return 0xFFU; // unattached / floating bus (simplified)
            }
            const auto high = static_cast<std::uint8_t>(port >> 8U);
            std::uint8_t value = 0xFFU;
            for (int row = 0; row < 8; ++row) {
                if ((high & static_cast<std::uint8_t>(1U << row)) == 0U) {
                    value &= s->keyboard_rows[static_cast<std::size_t>(row)];
                }
            }
            value = s->ear_input ? static_cast<std::uint8_t>(value | 0x40U)
                                 : static_cast<std::uint8_t>(value & ~0x40U);
            return static_cast<std::uint8_t>(value | 0xA0U);
        });
        s->cpu.set_port_out([s](std::uint16_t port, std::uint8_t value) {
            if ((port & 0x0001U) == 0U) {
                s->ula.set_border(value);
            }
        });

        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on); // Z80 powers on at PC=$0000 (ROM)
        return sys;
    }

} // namespace mnemos::manifests::spectrum
