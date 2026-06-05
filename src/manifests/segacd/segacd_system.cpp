#include "segacd_system.hpp"

namespace mnemos::manifests::segacd {

    void segacd_system::run_cycles(std::uint64_t cycles) {
        if (sub_reset_asserted || sub_busreq) {
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
        sub_busreq = false;
        prg_ram.fill(0);
        word_ram.fill(0);
        backup_ram.fill(0);
        gate_array.fill(0);
        gate_array[0x03] = 0x01; // RET=1: the main CPU owns word RAM at power-on
        pcm.reset(chips::reset_kind::power_on);
    }

    std::uint8_t segacd_system::gate_read(std::uint8_t offset) noexcept {
        return gate_array[offset];
    }

    void segacd_system::gate_write_main(std::uint8_t offset, std::uint8_t value) {
        // $01 sub-CPU control: bit 0 RESET (1=release, 0=assert), bit 1 BUSREQ
        // (1=halt the sub-CPU). A 0->1 RESET edge re-boots the sub-CPU.
        if (offset == 0x01U) {
            const bool want_release = (value & 0x01U) != 0U;
            const bool want_busreq = (value & 0x02U) != 0U;
            const bool prev_release = (gate_array[0x01] & 0x01U) != 0U;
            gate_array[0x01] = value;
            if (want_release && !prev_release) {
                release_sub_reset();
            } else if (!want_release) {
                sub_reset_asserted = true;
            }
            if (!sub_reset_asserted) {
                sub_busreq = want_busreq;
            }
            return;
        }
        // $03 memory mode (main side): RET (bit 0) is read-only here; the main
        // CPU writes the PRG-RAM bank (bits 6-7), the 1M/2M mode (bit 2), and
        // DMNA (bit 1, hand word RAM to the sub-CPU).
        if (offset == 0x03U) {
            const std::uint8_t cur = gate_array[0x03];
            std::uint8_t next = static_cast<std::uint8_t>((cur & 0x01U) | (value & 0xC4U));
            if ((value & 0x02U) != 0U) {
                next = static_cast<std::uint8_t>((next & 0xFEU) | 0x02U); // DMNA: clear RET
            }
            gate_array[0x03] = next;
            return;
        }
        // Comm registers + the still-unwired CDC/CDD/timer/IRQ/stamp registers
        // default-store; their side effects arrive in B3 (IRQ) and phase C.
        gate_array[offset] = value;
    }

    void segacd_system::gate_write_sub(std::uint8_t offset, std::uint8_t value) {
        // $03 memory mode (sub side): the sub-CPU writes RET (bit 0) and MODE
        // (bit 2); the PRG bank + DMNA are main-side and preserved.
        if (offset == 0x03U) {
            const std::uint8_t cur = gate_array[0x03];
            std::uint8_t next = static_cast<std::uint8_t>((cur & 0xC2U) | (value & 0x05U));
            if ((value & 0x01U) != 0U) {
                next = static_cast<std::uint8_t>((next & 0xFDU) | 0x01U); // RET: clear DMNA
            }
            gate_array[0x03] = next;
            return;
        }
        gate_write_main(offset, value); // sub-side falls through for the rest
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
        // Word RAM $080000-$0BFFFF. The sub side always sees the full 256 KB;
        // 2M/1M ownership (RET/DMNA) is tracked in the gate-array $03 register.
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

        // Backup RAM $FE0000-$FE3FFF -- odd byte lane only (even bytes read 0).
        bus.map_mmio(
            0xFE0000U, static_cast<std::uint32_t>(backup_ram_size * 2U),
            [s](std::uint32_t a) -> std::uint8_t {
                const std::uint32_t off = a - 0xFE0000U;
                return ((off & 1U) != 0U) ? s->backup_ram[(off >> 1U) & (backup_ram_size - 1U)]
                                          : std::uint8_t{0};
            },
            [s](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t off = a - 0xFE0000U;
                if ((off & 1U) != 0U) {
                    s->backup_ram[(off >> 1U) & (backup_ram_size - 1U)] = v;
                }
            },
            0);
        // Gate-array sub-side mirrors at $FF8000 and $0FF800.
        for (const std::uint32_t mirror_base : {0x0FF800U, 0xFF8000U}) {
            bus.map_mmio(
                mirror_base, static_cast<std::uint32_t>(gate_array_size),
                [s, mirror_base](std::uint32_t a) {
                    return s->gate_read(static_cast<std::uint8_t>(a - mirror_base));
                },
                [s, mirror_base](std::uint32_t a, std::uint8_t v) {
                    s->gate_write_sub(static_cast<std::uint8_t>(a - mirror_base), v);
                },
                0);
        }
        s->gate_array[0x03] = 0x01; // RET=1 at power-on (main CPU owns word RAM)

        s->sub_cpu.attach_bus(s->sub_bus);
        s->pcm.reset(chips::reset_kind::power_on);
        // The sub-CPU stays held in reset (sub_reset_asserted == true) until the
        // main CPU releases it via the gate array (B2) / release_sub_reset().
        return sys;
    }

} // namespace mnemos::manifests::segacd
