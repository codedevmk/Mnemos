#include "segacd_machine.hpp"

#include <cstdio>
#include <cstdlib>

namespace mnemos::manifests::segacd {

    namespace {
        bool machine_trace_enabled() {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in diagnostic, not hot-path
#endif
            static const bool on = std::getenv("MNEMOS_SEGACD_TRACE") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return on;
        }
    } // namespace

    std::unique_ptr<segacd_machine> assemble_segacd_machine(std::vector<std::uint8_t> bios,
                                                            const genesis::genesis_config& config) {
        auto machine = std::make_unique<segacd_machine>();
        // The sub side gets its own copy of the BIOS (the sub-CPU $0 overlay).
        machine->sub = assemble_segacd(bios);
        // The Genesis main side boots the BIOS as its cartridge.
        machine->genesis = genesis::assemble_genesis(std::move(bios), config);

        segacd_system* sub = machine->sub.get();
        genesis::genesis_system* gen = machine->genesis.get();
        topology::bus& bus = machine->genesis->bus;

        // $A12000-$A120FF: gate array (main-side access). The 128 KB BIOS only
        // occupies $000000-$01FFFF, so these SCD windows sit in free address
        // space; priority 1 keeps them above any future overlay.
        bus.map_mmio(
            0xA12000U, 0x100U,
            [sub](std::uint32_t a) { return sub->gate_read(static_cast<std::uint8_t>(a & 0xFFU)); },
            [sub, gen](std::uint32_t a, std::uint8_t v) {
                if (machine_trace_enabled() && (a & 0xFFU) == 0x01U && (v & 0x01U) == 0U) {
                    std::fprintf(stderr, "[park] main_pc=%06X writes gate $01=%02X\n",
                                 gen->cpu.cpu_registers().pc, v);
                }
                sub->gate_write_main(static_cast<std::uint8_t>(a & 0xFFU), v);
            },
            1);

        // $020000-$03FFFF: sub-CPU PRG-RAM, a 128 KB bank window selected by the
        // memory-mode register's bits 6-7 ($03).
        bus.map_mmio(
            0x020000U, 0x20000U,
            [sub](std::uint32_t a) {
                const std::uint32_t base =
                    ((static_cast<std::uint32_t>(sub->gate_array[0x03]) >> 6U) & 0x03U) * 0x20000U;
                return sub->prg_ram[(base + (a & 0x1FFFFU)) & (prg_ram_size - 1U)];
            },
            [sub, gen](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t base =
                    ((static_cast<std::uint32_t>(sub->gate_array[0x03]) >> 6U) & 0x03U) * 0x20000U;
                const std::uint32_t off = (base + (a & 0x1FFFFU)) & (prg_ram_size - 1U);
                if (machine_trace_enabled() && off >= 0x100U && off <= 0x57FFU) {
                    std::fprintf(stderr, "[prgw] main_pc=%06X prg[%05X]=%02X\n",
                                 gen->cpu.cpu_registers().pc, off, v);
                }
                sub->prg_ram[off] = v;
            },
            1);

        // $200000-$23FFFF: word RAM (2M mode -- the full 256 KB).
        bus.map_mmio(
            0x200000U, 0x40000U,
            [sub](std::uint32_t a) {
                return sub->word_ram[(a - 0x200000U) & (word_ram_size - 1U)];
            },
            [sub](std::uint32_t a, std::uint8_t v) {
                sub->word_ram[(a - 0x200000U) & (word_ram_size - 1U)] = v;
            },
            1);

        return machine;
    }

} // namespace mnemos::manifests::segacd
