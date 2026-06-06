#include "segacd_system.hpp"

#include <cstdio>
#include <cstdlib>

namespace mnemos::manifests::segacd {

    namespace {
        bool gate_trace_enabled() {
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
        sub_cpu.reset(chips::reset_kind::hard); // re-fetch the $0/$4 reset vectors
        update_sub_irq();                       // re-apply any pending IRQ now that we run
    }

    void segacd_system::reset() {
        sub_reset_asserted = true;
        sub_busreq = false;
        prg_ram.fill(0);
        word_ram.fill(0);
        backup_ram.fill(0);
        gate_array.fill(0);
        gate_array[0x03] = 0x01; // RET=1: the main CPU owns word RAM at power-on
        sub_irq_mask = 0;
        sub_irq_pending = 0;
        cdd_command.fill(0);
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
        stamp_size = 0;
        stamp_map_addr = 0;
        img_buf_v_cell = 0;
        img_buf_vector = 0;
        img_v_step = 0;
        img_h_step = 0;
        img_buf_width = 0;
        img_buf_height = 0;
        img_buf_offset = 0;
        trace_vector_addr = 0;
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
        return gate_array[offset];
    }

    void segacd_system::gate_write_main(std::uint8_t offset, std::uint8_t value) {
        // $01 sub-CPU control: bit 0 RESET (1=release, 0=assert), bit 1 BUSREQ
        // (1=halt the sub-CPU). A 0->1 RESET edge re-boots the sub-CPU.
        if (offset == 0x01U) {
            const bool want_release = (value & 0x01U) != 0U;
            const bool want_busreq = (value & 0x02U) != 0U;
            const bool prev_release = (gate_array[0x01] & 0x01U) != 0U;
            if (gate_trace_enabled() &&
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
        gate_array[offset] = value;
        // $00 bit 0 IFL2: the main CPU pulses the sub-CPU level-2 IRQ.
        if (offset == 0x00U && (value & 0x01U) != 0U) {
            raise_sub_irq(irq_ifl2);
        }
        // $33 sub-CPU IRQ mask.
        if (offset == 0x33U) {
            sub_irq_mask = value;
            update_sub_irq();
        }
        // $42-$4B CDD command buffer; writing $4B commits the command.
        if (offset >= 0x42U && offset <= 0x4BU) {
            cdd_command[offset - 0x42U] = value;
            if (offset == 0x4BU) {
                cdd_process_command();
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
        // $58-$6B stamp / rotation ASIC config (ROT triggers on $59 bit 0).
        if (offset >= 0x58U && offset <= 0x6BU) {
            stamp_reg_write(offset, value);
        }
    }

    void segacd_system::gate_write_sub(std::uint8_t offset, std::uint8_t value) {
        if (gate_trace_enabled() && (offset == 0x0EU || offset == 0x0FU)) {
            std::fprintf(stderr, "[comm] pc=%06X sub writes gate $%02X = %02X\n",
                         sub_cpu.cpu_registers().pc, offset, value);
        }
        // $01 (sub-CPU RESET / BUSREQ) is MAIN-side ONLY -- the sub-CPU cannot
        // reset or halt itself. The sub-CPU BIOS writes $FF8001 (e.g. `bclr #0`)
        // during init; routing that into gate_write_main's reset logic would make
        // the sub reset ITSELF mid-startup, so it never finishes its checksum nor
        // signals the main via $0F -- and no disc/game ever boots. Ignore it here.
        if (offset == 0x01U) {
            return;
        }
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
        // $33 sub-side IRQ mask.
        if (offset == 0x33U) {
            sub_irq_mask = value;
            gate_array[0x33] = value;
            update_sub_irq();
            return;
        }
        // $36 bit 0 acknowledges (clears) all pending sub-CPU IRQs.
        if (offset == 0x36U && (value & 0x01U) != 0U) {
            sub_irq_pending = 0U;
            gate_array[0x36] = value;
            update_sub_irq();
            return;
        }
        // $06 = CDC register-data write port (sub side; main side is $07).
        if (offset == 0x06U) {
            gate_array[0x06] = value;
            cdc_reg_w(value);
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

    std::unique_ptr<segacd_system> assemble_segacd(std::vector<std::uint8_t> bios) {
        auto sys = std::make_unique<segacd_system>();
        sys->bios = std::move(bios);
        auto* s = sys.get();
        topology::bus& bus = s->sub_bus;

        // PRG-RAM $000000-$07FFFF (read/write). The sub-CPU boots its reset
        // vectors ($0 SSP / $4 PC) from HERE: the main BIOS loads the Sub-CPU
        // BIOS + its vectors into PRG-RAM before releasing the sub. A BIOS
        // read-overlay at $0 (as the reference has) would shadow those vectors
        // with the MAIN entry -- whose stack lives in main work RAM, unmapped on
        // the sub bus -- crashing the sub. So there is intentionally NO overlay.
        bus.map_ram(0x000000U, s->prg_ram, 0);
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
        // Interrupt-acknowledge: when the sub-CPU accepts an IRQ, the gate array
        // clears that level's pulse/request flag (the IACK cycle). Without this
        // the request stays pending and the sub re-takes the same level forever
        // -- e.g. the main's IFL2 (level 2), which deadlocks the BIOS boot
        // handshake. Mirrors how the Genesis VDP V-blank IRQ self-clears on ack.
        s->sub_cpu.set_irq_ack_callback([s](int level) {
            if (gate_trace_enabled()) {
                std::fprintf(stderr, "[iack] L%d\n", level);
            }
            s->sub_irq_pending &= static_cast<std::uint8_t>(~(1U << level));
            s->update_sub_irq();
        });
        s->pcm.reset(chips::reset_kind::power_on);
        // The sub-CPU stays held in reset (sub_reset_asserted == true) until the
        // main CPU releases it via the gate array (B2) / release_sub_reset().
        return sys;
    }

} // namespace mnemos::manifests::segacd
