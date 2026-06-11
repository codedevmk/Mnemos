#include "segacd_machine.hpp"

#include <cstdio>
#include <cstdlib>

namespace mnemos::manifests::segacd {

    void segacd_machine::begin_comm_slice() noexcept {
        // Fold the just-ended slice's fractional sub-cycle remainder into the carry
        // before re-baselining, so the sub's long-term rate tracks 87.5/53.693175
        // exactly instead of losing the sub-cycle truncation every slice. (The
        // trailing catch_up_sub() leaves the sub at the slice boundary, so the full
        // slice delta below is the amount that was just run.)
        const std::uint64_t main_now = genesis->cpu.elapsed_cycles();
        sub_cycle_carry_ =
            ((main_now - slice_base_main_) * 87'500'000ULL + sub_cycle_carry_) % 53'693'175ULL;
        slice_base_main_ = main_now;
        slice_base_sub_ = sub->sub_position();
    }

    void segacd_machine::catch_up_sub() {
        // Run the sub-CPU up to the main's position within the current slice. Sub
        // cycles per main 68k cycle = 12.5 MHz / (53.693175 MHz / 7) = 87.5/53.693175.
        const std::uint64_t main_now = genesis->cpu.elapsed_cycles();
        if (main_now <= slice_base_main_) {
            return;
        }
        const std::uint64_t main_delta = main_now - slice_base_main_;
        const std::uint64_t target =
            slice_base_sub_ + (main_delta * 87'500'000ULL + sub_cycle_carry_) / 53'693'175ULL;
        const std::uint64_t cur = sub->sub_position();
        if (target > cur) {
            sub->run_cycles(target - cur); // no-op while the sub is held in reset
        }
    }

    std::unique_ptr<segacd_machine> assemble_segacd_machine(std::vector<std::uint8_t> bios,
                                                            const genesis::genesis_config& config) {
        auto machine = std::make_unique<segacd_machine>();
        // The sub side boots from PRG-RAM (the main BIOS loads the Sub-CPU BIOS
        // there at runtime), so it needs no BIOS image of its own.
        machine->sub = assemble_segacd();
        // The Genesis main side boots the BIOS as its cartridge.
        machine->genesis = genesis::assemble_genesis(std::move(bios), config);

        segacd_system* sub = machine->sub.get();
        genesis::genesis_system* gen = machine->genesis.get();
        topology::bus& bus = machine->genesis->bus;
        segacd_machine* m = machine.get(); // for the comm poll-sync in the gate bridge

        // $A12000-$A120FF: gate array (main-side access). The 128 KB BIOS only
        // occupies $000000-$01FFFF, so these SCD windows sit in free address
        // space; priority 1 keeps them above any future overlay.
        bus.map_mmio(
            0xA12000U, 0x100U,
            [m](std::uint32_t a) {
                const auto off = static_cast<std::uint8_t>(a & 0xFFU);
                // Fence EVERY gate read: run the sub up to the main's cycle so the
                // main reads the sub's CURRENT state, never a stale one. The whole
                // window is shared protocol state (comm flags/words, DMNA/RET in
                // $03, CDD/CDC status), and a partial fence leaves orderings where
                // the two sides each act on the other's past -- the post-logo
                // word-RAM handshake converges deadlocked that way.
                m->catch_up_sub();
                return m->sub->gate_read(off);
            },
            [m](std::uint32_t a, std::uint8_t v) {
                const auto off = static_cast<std::uint8_t>(a & 0xFFU);
                // Fence EVERY gate write: the sub must consume its timeline up to
                // "now" BEFORE the write lands, or it observes the main's future
                // early (an IFL2 pulse via $00 or a DMNA grant via $03 becoming
                // visible mid-burst inverts the protocol order the BIOS relies on).
                m->catch_up_sub();
                if (segacd_trace_enabled() && off == 0x01U && (v & 0x01U) == 0U) {
                    std::fprintf(stderr, "[park] main_pc=%06X writes gate $01=%02X\n",
                                 m->genesis->cpu.cpu_registers().pc, v);
                }
                m->sub->gate_write_main(off, v);
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
                if (segacd_trace_enabled() && off >= 0x100U && off <= 0x5FFFU) {
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
