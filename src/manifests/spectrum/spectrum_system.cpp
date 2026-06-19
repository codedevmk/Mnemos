#include "spectrum_system.hpp"

#include <cstring>

namespace mnemos::manifests::spectrum {

    namespace {
        constexpr std::uint32_t k_state_version = 2U;

        [[nodiscard]] std::span<const std::uint8_t>
        screen_view(const std::array<std::uint8_t, 0x4000>& bank) {
            return std::span<const std::uint8_t>(bank).first(chips::video::ula::screen_ram_bytes);
        }
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

    void spectrum_system::set_paging(std::uint8_t value) noexcept {
        if (paging_locked) {
            return;
        }
        port_7ffd = value;
        // $C000: RAM bank (bits 0-2). retarget swaps the existing region's span.
        bus.retarget_ram(0xC000U, std::span<std::uint8_t>(ram_bank[value & 0x07U]));
        // $0000: ROM half (bit 4) -- 0 = 128K editor, 1 = 48 BASIC.
        const std::size_t rom_half = ((value >> 4U) & 1U) * 0x4000U;
        bus.retarget_rom(0x0000U, std::span<const std::uint8_t>(rom).subspan(rom_half, 0x4000U));
        // Display bank: 5 normally, 7 (shadow) when bit 3 is set.
        ula.set_screen_ram(screen_view(ram_bank[(value & 0x08U) != 0U ? 7U : 5U]));
        if ((value & 0x20U) != 0U) {
            paging_locked = true; // locked until the next reset
        }
    }

    void spectrum_system::apply_snapshot(const spectrum_snapshot& snap) noexcept {
        cpu.set_registers(snap.regs);
        ula.set_border(snap.border);
        if (model == spectrum_model::k128 && snap.is_128k) {
            ram_bank = snap.bank;
            paging_locked = false; // let the snapshot's own latch take effect
            set_paging(snap.port_7ffd);
        } else {
            // 48K-visible banks (a 48K snapshot, or a 128K snapshot on a 48K core).
            ram_bank[5] = snap.bank[5];
            ram_bank[2] = snap.bank[2];
            ram_bank[0] = snap.bank[0];
        }
    }

    void spectrum_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.bytes(keyboard_rows);
        writer.boolean(ear_input);
        writer.u8(port_7ffd);
        writer.boolean(paging_locked);
    }

    void spectrum_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        reader.bytes(keyboard_rows);
        ear_input = reader.boolean();
        port_7ffd = reader.u8();
        paging_locked = reader.boolean();
    }

    std::unique_ptr<spectrum_system> assemble_spectrum(std::span<const std::uint8_t> rom,
                                                       const spectrum_config& config) {
        auto sys = std::make_unique<spectrum_system>();
        spectrum_system* s = sys.get();
        s->keyboard_rows.fill(0xFFU); // all keys released
        s->model = (config.model == spectrum_model::k128 || rom.size() >= 0x8000U)
                       ? spectrum_model::k128
                       : spectrum_model::k48;

        const std::size_t n = rom.size() < s->rom.size() ? rom.size() : s->rom.size();
        std::memcpy(s->rom.data(), rom.data(), n);

        // Initial layout (both models): $0000 = ROM half 0, $4000 = RAM bank 5 (the
        // screen), $8000 = bank 2, $C000 = bank 0. The 128K re-pages $0000/$C000 and
        // the screen through set_paging (retargeting these regions); the 48K leaves
        // them fixed.
        s->bus.map_rom(0x0000U, std::span<const std::uint8_t>(s->rom).first(0x4000U), 0);
        s->bus.map_ram(0x4000U, std::span<std::uint8_t>(s->ram_bank[5]), 0);
        s->bus.map_ram(0x8000U, std::span<std::uint8_t>(s->ram_bank[2]), 0);
        s->bus.map_ram(0xC000U, std::span<std::uint8_t>(s->ram_bank[0]), 0);
        s->ula.set_screen_ram(screen_view(s->ram_bank[5]));

        s->ula.set_irq_callback([s](bool asserted) { s->cpu.set_irq_line(asserted); });

        // Z80 I/O space. The ULA decodes A0 = 0 (canonically $FE): reads return the
        // keyboard half-rows selected by the high address byte (active-low) with EAR
        // in bit 6; the Kempston joystick answers at $1F.
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
                s->ula.set_border(value);                     // bits 0-2
                s->beeper.set_speaker((value & 0x10U) != 0U); // bit 4 (speaker)
            }
            // 128K paging latch $7FFD: A15 = 0 and A1 = 0 (the machine's loose decode).
            if (s->model == spectrum_model::k128 && (port & 0x8002U) == 0U) {
                s->set_paging(value);
            }
        });

        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on); // Z80 powers on at PC=$0000 (ROM)
        return sys;
    }

} // namespace mnemos::manifests::spectrum
