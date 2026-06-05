#include "segacd_system.hpp"

namespace mnemos::manifests::segacd {

    void segacd_system::run_cycles(std::uint64_t cycles) {
        if (sub_reset_asserted) {
            return;
        }
        sub_cpu.tick(cycles);
    }

    void segacd_system::release_sub_reset() {
        sub_reset_asserted = false;
        sub_cpu.reset(chips::reset_kind::hard); // re-fetch the $0/$4 reset vectors
    }

    void segacd_system::reset() {
        sub_reset_asserted = true;
        prg_ram.fill(0);
        word_ram.fill(0);
        backup_ram.fill(0);
        pcm.reset(chips::reset_kind::power_on);
    }

    std::unique_ptr<segacd_system> assemble_segacd(std::vector<std::uint8_t> bios) {
        auto sys = std::make_unique<segacd_system>();
        sys->bios = std::move(bios);
        auto* s = sys.get();
        topology::bus& bus = s->sub_bus;

        // PRG-RAM $000000-$07FFFF (read/write).
        bus.map_ram(0x000000U, s->prg_ram, 0);
        // BIOS read overlay: reads in $000000-$(bios_size-1) come from the boot
        // ROM, writes fall through to PRG-RAM underneath (priority 1, reads only).
        if (!s->bios.empty()) {
            bus.map_rom(0x000000U, s->bios, 1,
                        [](std::uint32_t /*addr*/, bool is_write) { return !is_write; });
        }
        // Word RAM $080000-$0BFFFF (2M mode; 1M/1M split is added with the gate
        // array in B2).
        bus.map_ram(0x080000U, s->word_ram, 0);
        // RF5C164 register window $FF0000-$FF0FFF ($00-$08).
        bus.map_mmio(
            0xFF0000U, 0x1000U,
            [s](std::uint32_t a) { return s->pcm.read_reg(static_cast<std::uint8_t>(a & 0x0FU)); },
            [s](std::uint32_t a, std::uint8_t v) {
                s->pcm.write_reg(static_cast<std::uint8_t>(a & 0x0FU), v);
            },
            0);
        // RF5C164 wave-RAM window $FF1000-$FF1FFF (bank-selected by CTRL).
        bus.map_mmio(
            0xFF1000U, 0x1000U,
            [s](std::uint32_t a) {
                return s->pcm.read_waveram(static_cast<std::uint16_t>(a & 0x0FFFU));
            },
            [s](std::uint32_t a, std::uint8_t v) {
                s->pcm.write_waveram(static_cast<std::uint16_t>(a & 0x0FFFU), v);
            },
            0);

        s->sub_cpu.attach_bus(s->sub_bus);
        s->pcm.reset(chips::reset_kind::power_on);
        // The sub-CPU stays held in reset (sub_reset_asserted == true) until the
        // main CPU releases it via the gate array (B2) / release_sub_reset().
        return sys;
    }

} // namespace mnemos::manifests::segacd
