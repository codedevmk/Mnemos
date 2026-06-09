#include "sega32x_system.hpp"

namespace mnemos::manifests::sega32x {

    std::uint8_t sega32x_system::comm_read(std::uint32_t offset) const noexcept {
        const std::uint16_t word = comm[(offset >> 1U) & (comm_words - 1U)];
        return (offset & 1U) != 0U ? static_cast<std::uint8_t>(word)
                                   : static_cast<std::uint8_t>(word >> 8U);
    }

    void sega32x_system::comm_write(std::uint32_t offset, std::uint8_t value) noexcept {
        std::uint16_t& word = comm[(offset >> 1U) & (comm_words - 1U)];
        if ((offset & 1U) != 0U) {
            word = static_cast<std::uint16_t>((word & 0xFF00U) | value);
        } else {
            word = static_cast<std::uint16_t>((word & 0x00FFU) |
                                              (static_cast<std::uint16_t>(value) << 8U));
        }
    }

    void sega32x_system::reset() {
        sh2_reset_asserted = true;
        adapter_ctrl = 0U;
        comm.fill(0U);
        // The buses are already attached, so reset reads each CPU's PC/SP from its
        // own BIOS reset vectors at $0.
        master_cpu.reset(chips::reset_kind::power_on);
        slave_cpu.reset(chips::reset_kind::power_on);
    }

    void sega32x_system::run_cycles(std::uint64_t cycles) {
        if (sh2_reset_asserted) {
            return; // both SH-2s held inactive
        }
        master_cpu.tick(cycles);
        slave_cpu.tick(cycles);
    }

    std::unique_ptr<sega32x_system> assemble_sega32x() {
        auto sys = std::make_unique<sega32x_system>();
        auto* s = sys.get();

        // Both CPUs share the SDRAM, frame buffer, and COMM bank but each boots
        // from its own ROM at $0.
        s->master_bus.map_rom(bios_base, s->m_bios, 0);
        s->slave_bus.map_rom(bios_base, s->s_bios, 0);
        for (topology::bus* bus : {&s->master_bus, &s->slave_bus}) {
            bus->map_ram(framebuffer_base, s->framebuffer, 0);
            bus->map_ram(sdram_base, s->sdram, 0);
            bus->map_mmio(
                comm_base, static_cast<std::uint32_t>(comm_words * 2U),
                [s](std::uint32_t a) { return s->comm_read(a - comm_base); },
                [s](std::uint32_t a, std::uint8_t v) { s->comm_write(a - comm_base, v); }, 0);
        }

        s->master_cpu.attach_bus(s->master_bus);
        s->slave_cpu.attach_bus(s->slave_bus);
        return sys;
    }

} // namespace mnemos::manifests::sega32x
