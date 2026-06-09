#include "sega32x_system.hpp"

#include <array>

namespace mnemos::manifests::sega32x {

    namespace {
        struct irq_source {
            std::uint8_t bit;
            int level;
            std::uint8_t vector;
        };
        // The four 32X interrupt sources, highest priority first.
        constexpr std::array<irq_source, 4> irq_sources{{
            {0x01U, 12, 0x44U}, // VINT
            {0x02U, 10, 0x46U}, // HINT
            {0x04U, 8, 0x48U},  // CMD
            {0x08U, 6, 0x4AU},  // PWM
        }};
    } // namespace

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

    void sega32x_system::deliver_irq(std::uint8_t bit, int level, std::uint8_t vector) {
        if (sh2_reset_asserted) {
            return;
        }
        // Latch the edge on both CPUs regardless of mask; deliver to each that
        // enables it and whose pending level it outranks (consuming the latch).
        master_irq_latch |= bit;
        slave_irq_latch |= bit;
        if ((master_irq_mask & bit) != 0U && level > master_cpu.pending_irq_level()) {
            master_cpu.set_irq(level, vector);
            master_irq_latch &= static_cast<std::uint8_t>(~bit);
        }
        if ((slave_irq_mask & bit) != 0U && level > slave_cpu.pending_irq_level()) {
            slave_cpu.set_irq(level, vector);
            slave_irq_latch &= static_cast<std::uint8_t>(~bit);
        }
    }

    void sega32x_system::fire_latched(chips::cpu::sh2& cpu, std::uint8_t mask,
                                      std::uint8_t& latch) {
        if (sh2_reset_asserted) {
            return;
        }
        for (const irq_source& src : irq_sources) {
            if ((latch & src.bit) == 0U || (mask & src.bit) == 0U) {
                continue;
            }
            if (cpu.pending_irq_level() == src.level && cpu.pending_irq_vector() == src.vector) {
                latch &= static_cast<std::uint8_t>(~src.bit); // already presented
                continue;
            }
            if (src.level > cpu.pending_irq_level()) {
                cpu.set_irq(src.level, src.vector);
                latch &= static_cast<std::uint8_t>(~src.bit);
            }
        }
    }

    void sega32x_system::raise_vint() { deliver_irq(irq_vint, 12, 0x44U); }
    void sega32x_system::raise_hint() { deliver_irq(irq_hint, 10, 0x46U); }
    void sega32x_system::raise_cmd() { deliver_irq(irq_cmd, 8, 0x48U); }
    void sega32x_system::raise_pwm() { deliver_irq(irq_pwm, 6, 0x4AU); }

    void sega32x_system::set_master_irq_mask(std::uint8_t mask) {
        master_irq_mask = mask;
        fire_latched(master_cpu, master_irq_mask, master_irq_latch);
    }

    void sega32x_system::set_slave_irq_mask(std::uint8_t mask) {
        slave_irq_mask = mask;
        fire_latched(slave_cpu, slave_irq_mask, slave_irq_latch);
    }

    void sega32x_system::reset() {
        sh2_reset_asserted = true;
        adapter_ctrl = 0U;
        comm.fill(0U);
        master_irq_mask = 0U;
        slave_irq_mask = 0U;
        master_irq_latch = 0U;
        slave_irq_latch = 0U;
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

        // When a CPU accepts an IRQ, re-scan its latched sources so a waiting
        // lower-priority edge is delivered rather than stranded.
        s->master_cpu.set_irq_accept_callback([s](int, std::uint8_t) { s->master_irq_accept(); });
        s->slave_cpu.set_irq_accept_callback([s](int, std::uint8_t) { s->slave_irq_accept(); });
        return sys;
    }

} // namespace mnemos::manifests::sega32x
