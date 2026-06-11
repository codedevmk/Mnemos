#include "segacd_system.hpp"

#include <cstdio>
#include <cstdlib>

namespace mnemos::manifests::segacd {

    // Opt-in disc-boot debug tracing (MNEMOS_SEGACD_TRACE), shared by the gate
    // array, CDD, and the main-side bridge.
    bool segacd_trace_enabled() noexcept {
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

    void segacd_system::run_cycles(std::uint64_t cycles) {
        if (sub_reset_asserted || sub_busreq) {
            return;
        }
        sub_cpu.tick(cycles);
        cdc_dma_run(); // service any armed CDC memory DMA

        // Sub-CPU timer: raise the level-3 IRQ once per (timer_word+1)*385 cycles
        // (~30.72 us). timer_word == 0 disables it.
        if (timer_word != 0U) {
            timer_cycle_acc += static_cast<std::uint32_t>(cycles);
            const std::uint32_t period = (static_cast<std::uint32_t>(timer_word) + 1U) * 385U;
            while (timer_cycle_acc >= period) {
                timer_cycle_acc -= period;
                raise_sub_irq(irq_timer);
            }
        }
    }

    void segacd_system::release_sub_reset() {
        sub_reset_asserted = false;
        // The CPU reset zeroes elapsed_cycles(); fold the discarded count into
        // the base so sub_position() stays continuous across the edge.
        sub_elapsed_base += sub_cpu.elapsed_cycles();
        sub_cpu.reset(chips::reset_kind::hard); // re-fetch the $0/$4 reset vectors
        update_sub_irq();                       // re-apply any pending IRQ now that we run
    }

    void segacd_system::sub_peripheral_reset() {
        // Soft CD-hardware reset (sub-side $01 bit 0 cleared). Matches the
        // reference: clear ONLY the sub-side registers -- the comm flags/words
        // $0E-$2F are preserved -- then re-apply the power-on defaults and reset
        // the drive, CDC, GFX and PCM. The published CDD status frame becomes
        // the pristine all-zero frame (RS0-RS8 = 0, RS9 checksum = $F) the
        // BIOS's drive-init op validates against.
        gate_array[0x04] = 0;
        gate_array[0x05] = 0;
        gate_array[0x0C] = 0;
        gate_array[0x0D] = 0;
        for (std::size_t i = 0x30U; i < gate_array.size(); ++i) {
            gate_array[i] = 0;
        }
        gate_array[0x08] = 0xFFU;
        gate_array[0x09] = 0xFFU;
        gate_array[0x0A] = 0xFFU;
        gate_array[0x0B] = 0xFFU;
        gate_array[0x36] = 0x01U;
        gate_array[0x41] = 0x0FU; // RS9 of the pristine CDD status frame
        for (std::size_t i = 0x42U; i <= 0x4BU; ++i) {
            gate_array[i] = 0xFFU;
        }
        sub_irq_mask = 0;
        sub_irq_pending = 0;
        update_sub_irq();
        timer_word = 0;
        timer_cycle_acc = 0;
        // Drive (the reference's cdd_reset): position/latency cleared, status
        // back to TOC-with-disc, CD-DA silenced.
        cdd_command.fill(0xFFU);
        cdd_status.fill(0);
        cdd_status[9] = 0x0FU;
        cdd_pending_status = 0;
        cdd_latency = 0;
        cdd_play_warmup = 0;
        cdd_lba = 0;
        cdd_track = 0;
        cdd_drive_status = cdd_loaded ? std::uint8_t{cdd_toc} : std::uint8_t{cdd_nodisc};
        cdda_stop();
        // CDC registers (the reference's cdc_reset; the 16 KB ring is kept).
        cdc_ifstat = 0xFFU;
        cdc_ifctrl = 0;
        cdc_dbc = 0;
        cdc_dac = 0;
        cdc_pt = 0;
        cdc_wa = 0;
        cdc_ctrl = {};
        cdc_head = {};
        cdc_head[0][3] = 0x01U; // mode byte defaults to Mode 1
        cdc_stat = {};
        cdc_stat[3] = 0x80U; // VALST
        cdc_ar = 0;
        cdc_irq = 0;
        cdc_dma_dest = 0;
        pcm.reset(chips::reset_kind::power_on);
    }

    void segacd_system::reset() {
        sub_reset_asserted = true;
        sub_elapsed_base = 0U;
        sub_busreq = false;
        prg_ram.fill(0);
        word_ram.fill(0);
        backup_ram.fill(0);
        gate_array.fill(0);
        gate_array[0x03] = 0x01; // RET=1: the main CPU owns word RAM at power-on
        dmna_pending = false;
        // Sub-side gate-register power-on defaults (match the reference): $08/$0A =
        // $FFFF, $36 = $0100, $40 = $000F (status RS9 trailer), $42-$4B = $FFFF -- the
        // CDD-command "idle" sentinel the BIOS sees before issuing a new command.
        gate_array[0x08] = 0xFFU;
        gate_array[0x09] = 0xFFU;
        gate_array[0x0A] = 0xFFU;
        gate_array[0x0B] = 0xFFU;
        gate_array[0x36] = 0x01U;
        gate_array[0x41] = 0x0FU;
        for (std::size_t i = 0x42U; i <= 0x4BU; ++i) {
            gate_array[i] = 0xFFU;
        }
        sub_irq_mask = 0;
        sub_irq_pending = 0;
        cdd_command.fill(0xFFU); // CDD command regs idle = $FF (matches the reference)
        cdd_status.fill(0);
        cdd_pending_status = 0;
        cdd_latency = 0;
        cdd_lba = 0;
        cdd_track = 0;
        cdd_drive_status = cdd_loaded ? std::uint8_t{cdd_toc} : std::uint8_t{cdd_nodisc};
        cdda_active = false;
        cdda_current_lba = 0;
        cdda_sample_in_sector = 0;
        cdda_loop = false;
        cdc_ram.fill(0);
        cdc_ifstat = 0xFFU;
        cdc_ifctrl = 0;
        cdc_dbc = 0;
        cdc_dac = 0;
        cdc_pt = 0;
        cdc_wa = 0;
        cdc_ctrl = {};
        cdc_head = {};
        cdc_stat = {};
        cdc_stat[3] = 0x80U; // VALST
        cdc_ar = 0;
        cdc_irq = 0;
        cdc_dma_dest = 0;
        timer_word = 0;
        timer_cycle_acc = 0;
        pcm.reset(chips::reset_kind::power_on);
    }

    std::uint8_t segacd_system::gate_read(std::uint8_t offset) {
        // $06/$07 are the CDC indirect register data port (the reference left
        // cdc_reg_r unwired; reading it here makes CDC register reads work).
        if (offset == 0x06U || offset == 0x07U) {
            return cdc_reg_r();
        }
        // $09 is the low byte of the CDC host-read word; consuming it stages the
        // next word (the high byte at $08 was read just before).
        if (offset == 0x09U) {
            const std::uint8_t lo = gate_array[0x09];
            cdc_host_advance();
            return lo;
        }
        // $50-$57: the font expander. The glyph word at $4E-$4F holds 16 1bpp
        // pixels; each expanded word maps 4 of them to the two 4-bit colors in
        // $4D. The BIOS/games render text through this unit -- raw register
        // bytes here garble every glyph.
        if (offset >= 0x50U && offset <= 0x57U) {
            const auto font =
                static_cast<std::uint16_t>((gate_array[0x4E] << 8) | gate_array[0x4F]);
            const std::uint8_t code = gate_array[0x4D];
            auto bits = static_cast<std::uint8_t>((font >> (((offset & 6U) ^ 6U) << 1U)) << 2U);
            auto data = static_cast<std::uint16_t>((code >> (bits & 4U)) & 0x0FU);
            bits = static_cast<std::uint8_t>(bits >> 1U);
            data = static_cast<std::uint16_t>(data | (((code >> (bits & 4U)) << 4U) & 0xF0U));
            bits = static_cast<std::uint8_t>(bits >> 1U);
            data = static_cast<std::uint16_t>(data | (((code >> (bits & 4U)) << 8U) & 0xF00U));
            bits = static_cast<std::uint8_t>(bits >> 1U);
            data = static_cast<std::uint16_t>(data | (((code >> (bits & 4U)) << 12U) & 0xF000U));
            return ((offset & 1U) != 0U) ? static_cast<std::uint8_t>(data)
                                         : static_cast<std::uint8_t>(data >> 8U);
        }
        // Diagnostic: the sub copies the main->sub comm payload $10-$1F (the CDBIOS
        // $6162 copy). Log any NONZERO read to confirm whether the sub ever captures
        // a real command packet, or only reads $10-$1F during 0-periods (timing miss).
        if (offset >= 0x10U && offset <= 0x1FU && gate_array[offset] != 0U &&
            segacd_trace_enabled()) {
            std::fprintf(stderr, "[commrd] sub reads comm $%02X = %02X\n", offset,
                         gate_array[offset]);
        }
        return gate_array[offset];
    }

    void segacd_system::gate_write_main(std::uint8_t offset, std::uint8_t value) {
        // $01 sub-CPU control: bit 0 RESET (1=release, 0=assert), bit 1 BUSREQ
        // (1=halt the sub-CPU). A 0->1 RESET edge re-boots the sub-CPU.
        if (offset == 0x01U) {
            const bool want_release = (value & 0x01U) != 0U;
            const bool want_busreq = (value & 0x02U) != 0U;
            const bool prev_release = (gate_array[0x01] & 0x01U) != 0U;
            if (segacd_trace_enabled() &&
                (want_release != prev_release || want_busreq != sub_busreq)) {
                std::fprintf(stderr, "[gate01] val=%02X %s busreq=%d sub_elapsed=%llu\n", value,
                             want_release ? "RELEASE" : "PARK", static_cast<int>(want_busreq),
                             static_cast<unsigned long long>(sub_cpu.elapsed_cycles()));
            }
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
        // $03 memory mode (main side): RET (bit 0) + MODE (bit 2) are sub-owned;
        // the main CPU writes the PRG-RAM bank (bits 6-7) and DMNA (bit 1),
        // whose meaning depends on the mode.
        if (offset == 0x03U) {
            // 2M: DMNA=1 hands word RAM to the sub (DMNA set, RET cleared);
            // DMNA=0 is a no-op (bank-bits-only update). 1M: DMNA=1 only ARMS
            // the "return word RAM to the sub on the 2M exit" request, readback
            // unchanged; DMNA=0 quirk-SETS the readback DMNA bit (hardware
            // behavior the BIOS relies on) without any swap.
            const std::uint8_t cur = gate_array[0x03];
            std::uint8_t next = static_cast<std::uint8_t>((cur & 0x3FU) | (value & 0xC0U));
            if ((cur & 0x04U) != 0U) { // 1M mode
                if ((value & 0x02U) != 0U) {
                    dmna_pending = true;
                } else {
                    next |= 0x02U;
                }
            } else if ((value & 0x02U) != 0U) {
                next = static_cast<std::uint8_t>((next & 0xFEU) |
                                                 0x02U); // DMNA: take word RAM, clear RET
            }
            if (segacd_trace_enabled()) {
                std::fprintf(stderr, "[wram] main $03 %02X->%02X pend=%d\n", value, next,
                             static_cast<int>(dmna_pending));
            }
            gate_array[0x03] = next;
            return;
        }
        // $0E/$0F communication flag: the MAIN owns $0E. Writing EITHER byte lane
        // ($0E or $0F) updates $0E -- the byte-select strobe (!LWR) is ignored on
        // hardware. (The sub owns $0F symmetrically; see gate_write_sub.) Storing a
        // main "clear" to $0F instead of $0E is what hangs the boot comm handshake.
        if (offset == 0x0EU || offset == 0x0FU) {
            if (segacd_trace_enabled()) {
                std::fprintf(stderr, "[maincmd] main $%02X(comm)->$0E = %02X\n", offset, value);
            }
            gate_array[0x0E] = value;
            return;
        }
        if (segacd_trace_enabled() && (offset >= 0x10U && offset <= 0x1FU)) {
            std::fprintf(stderr, "[maincmd] main $%02X = %02X\n", offset, value);
        }
        gate_array[offset] = value;
        // $00 bit 0 IFL2: the main CPU pulses the sub-CPU level-2 IRQ -- but ONLY when L2
        // is enabled in the sub IRQ mask ($33 bit 2), matching the reference. A pulse while
        // L2 is masked is IGNORED, not latched; otherwise it fires spuriously the instant
        // the BIOS later unmasks L2 and enables interrupts -- the boot diverged off the
        // disc-read driver at sub PC $3FA into the L2 handler in exactly that way.
        if (offset == 0x00U && segacd_trace_enabled()) {
            std::fprintf(stderr, "[ifl2] main $00 = %02X (mask=%02X)\n", value, gate_array[0x33]);
        }
        if (offset == 0x00U && (value & 0x01U) != 0U && (gate_array[0x33] & irq_ifl2) != 0U) {
            raise_sub_irq(irq_ifl2);
        }
        // $33 sub-CPU IRQ mask.
        if (offset == 0x33U) {
            sub_irq_mask = value;
            // IFL2 is a LEVEL the gate array holds (gate $00 bit 0, driven
            // high/low by the main); enabling L2 while the flag is high delivers
            // the pending request. Without this, a main command pulse landing in
            // the sub's re-init window (mask momentarily 0) is silently lost, the
            // round-phase handshake ($FDDE bchg / $0F.1 toggle) loses one beat,
            // and the game-load rendezvous deadlocks (main $1288 / sub $6194).
            if ((value & irq_ifl2) != 0U && (gate_array[0x00] & 0x01U) != 0U) {
                raise_sub_irq(irq_ifl2);
            }
            update_sub_irq();
        }
        // $42-$4B CDD command buffer. The BIOS commits by writing $4A: $42-$49 hold
        // the command code + params, $4A is the commit trigger. Match the reference --
        // process on the $4A write and zero $4A afterward so the next command needs a
        // fresh commit. (Committing on $4B fires a byte late and misses any command
        // frame the BIOS terminates at $4A.)
        if (offset >= 0x42U && offset <= 0x4BU) {
            cdd_command[offset - 0x42U] = value;
            if (offset == 0x4AU) {
                cdd_process_command();
                cdd_command[0x08U] = 0; // $4A: clear the commit trigger
            }
        }
        // $05 = CDC register-address pointer; $07 = CDC register-data (main side).
        if (offset == 0x05U) {
            cdc_ar = static_cast<std::uint8_t>(value & 0x1FU);
        }
        if (offset == 0x07U) {
            cdc_reg_w(value);
        }
        // $31 = sub-CPU timer word; reprogramming restarts the countdown.
        if (offset == 0x31U) {
            timer_word = value;
            timer_cycle_acc = 0U;
        }
        // $58-$67 stamp / rotation ASIC config (GO = the $66 trace-vector-base
        // word commit, i.e. the $67 low-byte write).
        if (offset >= 0x58U && offset <= 0x67U) {
            stamp_reg_write(offset, value);
        }
    }

    void segacd_system::gate_write_sub(std::uint8_t offset, std::uint8_t value) {
        if (segacd_trace_enabled() && (offset == 0x0EU || offset == 0x0FU)) {
            std::fprintf(stderr, "[comm] pc=%06X sub writes gate $%02X = %02X\n",
                         sub_cpu.cpu_registers().pc, offset, value);
        }
        // $00 (sub side) is the LED-control register; gate $00 itself is
        // MAIN-side only (the IFL2 request flag). Falling through to
        // gate_write_main here routed the sub's own LED writes into the IFL2
        // trigger: the sub interrupted ITSELF with bogus L2 command-accepts,
        // each running an extra comm cycle that flipped the round-phase
        // handshake ($FDDE bchg vs $0F.1) out of step -- the game-load
        // rendezvous then deadlocked (main $1288 / sub $6194).
        if (offset == 0x00U) {
            sub_led = value;
            return;
        }
        // $01 (sub side): clearing bit 0 soft-resets the CD PERIPHERALS -- the
        // drive, CDC, GFX, timer and the sub-side gate registers (the comm
        // flags/words $0E-$2F survive) -- but NOT the sub CPU itself; CPU
        // reset/halt is the MAIN-side $01. Routing this into gate_write_main's
        // reset logic once made the sub reset ITSELF mid-startup (no boot);
        // dropping the write entirely instead broke the game launch: the BIOS's
        // drive-init op validates the pristine all-zero CDD frame (RS9=$F) this
        // reset publishes, and without it the op errors ($5A2E=$FF) and the
        // boot parks at the "Press the START BUTTON" re-init screen.
        if (offset == 0x01U) {
            if ((value & 0x01U) == 0U) {
                if (segacd_trace_enabled()) {
                    std::fprintf(stderr, "[subrst] pc=%06X CD peripheral soft reset\n",
                                 sub_cpu.cpu_registers().pc);
                }
                sub_peripheral_reset();
            }
            return;
        }
        // $03 memory mode (sub side): the sub-CPU writes RET (bit 0), MODE
        // (bit 2) and the write-protect bits (3-4); the PRG bank + DMNA are
        // main-side. The gate-array ignores the byte lane for this write, so
        // $FF8002 and $FF8003 both drive the low memory-mode byte.
        if (offset == 0x02U || offset == 0x03U) {
            const std::uint8_t cur = gate_array[0x03];
            std::uint8_t next;
            if ((value & 0x04U) != 0U) {
                // 1M mode (enter or stay): RET is the live bank-split select --
                // the strided bank views over the canonical 2M image re-route
                // instantly, so no content rearrangement is needed. DMNA
                // readback clears (swap completed); RET=1 also disarms a
                // pending 2M-exit return-to-sub.
                if ((value & 0x01U) != 0U) {
                    dmna_pending = false;
                }
                next = static_cast<std::uint8_t>((cur & 0xC0U) | (value & 0x1DU));
            } else {
                // 2M mode. On the 1M->2M exit RET comes back SET (word RAM to
                // the main) unless the main armed a return-to-sub via DMNA=1 in
                // 1M. Within 2M: the sub can only SET RET (return word RAM,
                // clearing DMNA); a write that does not set it preserves RET --
                // RET is cleared only by the main's DMNA -- otherwise a sub
                // $03=0 write wrongly drops RET and the main hangs at its
                // RET-wait.
                std::uint8_t v = value;
                if ((cur & 0x04U) != 0U) {
                    v = static_cast<std::uint8_t>(v | (dmna_pending ? 0x00U : 0x01U));
                    dmna_pending = false;
                }
                next = static_cast<std::uint8_t>(cur & 0xC3U);
                if ((v & 0x01U) != 0U) {
                    next = static_cast<std::uint8_t>((next & 0xFDU) |
                                                     0x01U); // RET: return word RAM, clear DMNA
                }
            }
            if (segacd_trace_enabled()) {
                std::fprintf(stderr, "[wram] sub  $03 %02X->%02X pend=%d pc=%06X\n", value, next,
                             static_cast<int>(dmna_pending), sub_cpu.cpu_registers().pc);
            }
            gate_array[0x03] = next;
            return;
        }
        // $33 sub-side IRQ mask.
        if (offset == 0x33U) {
            sub_irq_mask = value;
            gate_array[0x33] = value;
            // Level-held IFL2: deliver a pending main request on unmask (see the
            // main-side $33 handler for the full rationale).
            if ((value & irq_ifl2) != 0U && (gate_array[0x00] & 0x01U) != 0U) {
                raise_sub_irq(irq_ifl2);
            }
            update_sub_irq();
            return;
        }
        // $36 CDD control: only bit 2 (HOCK / CDD-comm enable) is writable and it
        // latches into $37; bits [1:0] read back 0. INT4 is gated on $37 bit 2, and
        // IRQ acknowledge is IACK-driven per-level -- $36 does NOT clear IRQs.
        if (offset == 0x36U) {
            gate_array[0x37] = static_cast<std::uint8_t>(value & 0x04U);
            if (segacd_trace_enabled()) {
                std::fprintf(stderr, "[hock] $36=%02X -> $37=%02X pc=%06X\n", value,
                             gate_array[0x37], sub_cpu.cpu_registers().pc);
            }
            return;
        }
        // $06 = CDC register-data write port (sub side; main side is $07).
        if (offset == 0x06U) {
            gate_array[0x06] = value;
            cdc_reg_w(value);
            return;
        }
        // $0E/$0F communication flag: the SUB owns $0F. Writing EITHER byte lane
        // ($0E or $0F) updates $0F -- the byte-select strobes (!LDS/!UDS) are
        // ignored on hardware. (The main owns $0E symmetrically.) This keeps a sub
        // write off the main's $0E so the main's flag stays main-controlled.
        if (offset == 0x0EU || offset == 0x0FU) {
            gate_array[0x0F] = value;
            return;
        }
        gate_write_main(offset, value); // sub-side falls through for the rest
    }

    int segacd_system::pending_irq_level() const noexcept {
        const auto active = static_cast<std::uint8_t>(sub_irq_pending & sub_irq_mask);
        for (int level = 6; level >= 1; --level) {
            if ((active & (1U << level)) != 0U) { // $33/pending: bit N = level N
                return level;
            }
        }
        return 0;
    }

    void segacd_system::acknowledge_irq(int level) {
        if (segacd_trace_enabled()) {
            std::fprintf(stderr, "[iack] L%d\n", level);
        }
        sub_irq_pending &= static_cast<std::uint8_t>(~(1U << level));
        // Level-2 (IFL2) acknowledge also retires the main-side IFL2 request flag
        // (gate $00 bit 0) so the boot comm handshake can advance.
        if (level == 2) {
            gate_array[0x00] &= static_cast<std::uint8_t>(~0x01U);
        }
        update_sub_irq();
    }

    void segacd_system::update_sub_irq() {
        if (sub_reset_asserted) {
            return;
        }
        sub_cpu.set_irq_level(pending_irq_level());
    }

    void segacd_system::raise_sub_irq(std::uint8_t source_bit) {
        sub_irq_pending = static_cast<std::uint8_t>(sub_irq_pending | source_bit);
        update_sub_irq();
    }

    std::unique_ptr<segacd_system> assemble_segacd() {
        auto sys = std::make_unique<segacd_system>();
        auto* s = sys.get();
        topology::bus& bus = s->sub_bus;

        // PRG-RAM $000000-$07FFFF (read/write). The sub-CPU boots its reset
        // vectors ($0 SSP / $4 PC) from HERE: the main BIOS loads the Sub-CPU
        // BIOS + its vectors into PRG-RAM before releasing the sub. A BIOS
        // read-overlay at $0 (as the reference has) would shadow those vectors
        // with the MAIN entry -- whose stack lives in main work RAM, unmapped on
        // the sub bus -- crashing the sub. So there is intentionally NO overlay.
        bus.map_ram(0x000000U, s->prg_ram, 0);
        // Word RAM, sub side. 2M mode: $080000-$0BFFFF is the full 256 KB
        // linearly (ownership tracked in gate $03). 1M mode: the sub's current
        // bank appears linearly at $0C0000-$0DFFFF (the window the BIOS->game
        // handoff streams the IP through) while $080000-$0BFFFF becomes the
        // dot-image view of that bank (one 4-bit pixel per window byte, writes
        // through the $03 PM priority mode).
        bus.map_mmio(
            0x080000U, 0x40000U,
            [s](std::uint32_t a) -> std::uint8_t {
                const std::uint32_t off = a - 0x080000U;
                return s->word_ram_1m() ? s->word_dot_read(off) : s->word_ram[off];
            },
            [s](std::uint32_t a, std::uint8_t v) {
                const std::uint32_t off = a - 0x080000U;
                if (s->word_ram_1m()) {
                    s->word_dot_write(off, v);
                } else {
                    s->word_ram[off] = v;
                }
            },
            0);
        bus.map_mmio(
            0x0C0000U, 0x20000U,
            [s](std::uint32_t a) -> std::uint8_t {
                return s->word_ram_1m() ? s->word_ram[segacd_system::word_bank_offset(
                                              s->sub_word_bank(), a - 0x0C0000U)]
                                        : std::uint8_t{0xFF};
            },
            [s](std::uint32_t a, std::uint8_t v) {
                if (s->word_ram_1m()) {
                    s->word_ram[segacd_system::word_bank_offset(s->sub_word_bank(),
                                                                a - 0x0C0000U)] = v;
                }
            },
            0);
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
        // Interrupt-acknowledge: when the sub-CPU accepts an IRQ, the gate array
        // clears that level's pulse/request flag (the IACK cycle). Without this
        // the request stays pending and the sub re-takes the same level forever
        // -- e.g. the main's IFL2 (level 2), which deadlocks the BIOS boot
        // handshake. Mirrors how the Genesis VDP V-blank IRQ self-clears on ack.
        s->sub_cpu.set_irq_ack_callback([s](int level) { s->acknowledge_irq(level); });
        s->pcm.reset(chips::reset_kind::power_on);
        // The sub-CPU stays held in reset (sub_reset_asserted == true) until the
        // main CPU releases it via the gate array (B2) / release_sub_reset().
        return sys;
    }

} // namespace mnemos::manifests::segacd
