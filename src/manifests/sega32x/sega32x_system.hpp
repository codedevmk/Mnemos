#pragma once

#include "bus.hpp"         // topology bus
#include "sega32x_vdp.hpp" // the 32X VDP (palette + autofill + composition)
#include "sh2.hpp"         // master + slave CPUs

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::sega32x {

    inline constexpr std::size_t sdram_size = 256U * 1024U;       // $06000000 shared work RAM
    inline constexpr std::size_t framebuffer_size = 256U * 1024U; // both frame-buffer banks
    inline constexpr std::size_t fb_bank_size = 128U * 1024U;     // one bank = one $04000000 view
    inline constexpr std::size_t m_bios_size = 2U * 1024U;        // master SH-2 boot ROM
    inline constexpr std::size_t s_bios_size = 1U * 1024U;        // slave SH-2 boot ROM
    inline constexpr std::size_t g_bios_size = 256U;              // 68000-side vector overlay
    inline constexpr std::size_t comm_words = 8U;                 // 8-word COMM bank
    inline constexpr std::size_t pwm_fifo_depth = 3U;             // per-channel PWM FIFO

    // SH-2 address map (both CPUs see the same layout, with per-CPU BIOS at $0).
    // The SH7604 partitions the 32-bit space by the top three address bits;
    // partitions 0/4/6 alias one physical layout (cached / cacheable-alias /
    // shadow) and partitions 1/5 alias the cache-through view, where the
    // cartridge replaces the boot ROM at offset 0. With no cache model the
    // aliases are plain mirrors, mapped per partition base.
    inline constexpr std::uint32_t bios_base = 0x00000000U;
    inline constexpr std::uint32_t cart_base = 0x02000000U; // cart window within a partition
    inline constexpr std::uint32_t framebuffer_base = 0x04000000U;
    inline constexpr std::uint32_t sdram_base = 0x06000000U;
    inline constexpr std::uint32_t cart_window_size = 0x400000U; // 4 MiB cart view
    inline constexpr std::uint32_t sysreg_base = 0x00004000U;    // SH-2-side system registers
    inline constexpr std::uint32_t sysreg_mirror = 0x20004000U;  // cache-through (P1) mirror
    inline constexpr std::uint32_t sysreg_size = 0x100U;         // 256-byte register window
    inline constexpr std::uint32_t comm_offset = 0x20U;          // COMM bank within the window
    inline constexpr std::uint32_t vdp_reg_base = 0x00004100U;   // 32X VDP register window
    inline constexpr std::uint32_t vdp_reg_size = 0x100U;
    inline constexpr std::uint32_t vdp_pal_base = 0x00004200U; // 32X VDP palette CRAM
    inline constexpr std::uint32_t vdp_pal_size = 0x200U;

    // Partition bases. p0: cached + cacheable alias + shadow (boot ROM at $0);
    // p1: cache-through views (cart at $0, the M_BIOS reads the header via
    // $22000400 and runs GBR-relative system-register access at $20004000).
    inline constexpr std::array<std::uint32_t, 3> p0_bases{0x00000000U, 0x80000000U, 0xC0000000U};
    inline constexpr std::array<std::uint32_t, 2> p1_bases{0x20000000U, 0xA0000000U};

    // Heap-allocated, never-moved 32X board (the "Mars" hardware). The two SH-2s
    // run on their own CPU-local buses that share the SDRAM, frame buffer, and the
    // COMM bank but see their own boot ROM at $0; the on-chip SH7604 peripherals
    // live inside each `sh2` (the $FFFFFE00 window). Built like segacd_system: the
    // buses hold spans into the member arrays and the MMIO handlers capture `this`.
    //
    // Built so far: the two CPUs + buses (BIOS/framebuffer/SDRAM), the reset
    // hold/release, the per-CPU 32X interrupt latches/masks, and the SH-2-side
    // system-register window (adapter control + the interrupt-enable register +
    // the COMM bank). Still to come: the INTC/FRT/WDT/DMAC behaviour, the 32X VDP +
    // PWM (phase C), and the Genesis-bus bridge + scheduling in the sega32x_machine
    // layer (phase D).
    struct sega32x_system final {
        chips::cpu::sh2 master_cpu;
        chips::cpu::sh2 slave_cpu;
        chips::video::sega32x_vdp vdp; // registers + palette; pixels live in framebuffer
        topology::bus master_bus{32U, topology::endianness::big};
        topology::bus slave_bus{32U, topology::endianness::big};

        std::array<std::uint8_t, sdram_size> sdram{};
        std::array<std::uint8_t, framebuffer_size> framebuffer{};
        // Per-CPU SH7604 cache data array at $C0000000: 4 KiB of on-chip RAM
        // when used as scratch (no cache behaviour is modelled). Retail slave
        // code copies hot loops here.
        std::array<std::uint8_t, 0x1000> master_cache_data{};
        std::array<std::uint8_t, 0x1000> slave_cache_data{};
        std::array<std::uint8_t, m_bios_size> m_bios{};
        std::array<std::uint8_t, s_bios_size> s_bios{};
        std::array<std::uint8_t, g_bios_size> g_bios{}; // 68000 $000000 vector overlay
        std::array<std::uint16_t, comm_words> comm{};   // shared 8-word COMM bank

        // Cartridge ROM view (borrowed -- the Genesis owns the bytes). Empty
        // until attach_cart wires the SH-2-side cart windows.
        std::span<const std::uint8_t> cart_rom{};

        std::uint16_t adapter_ctrl{};  // latched FM / RV bits + the V-blank status mirror
        bool adapter_enabled{};        // ADEN: set once by the cart's security block
        std::uint16_t hcount{};        // SH-2-side H-interrupt line-count register
        bool sh2_reset_asserted{true}; // SH-2s held in reset until the adapter releases them

        // 32X interrupt sources: a per-CPU enable mask + latch, each source with a
        // fixed SH-2 IRL level + vector. An edge latches on both CPUs regardless of
        // the mask; the mask only gates CPU-visible delivery, so a mask 0->1 write
        // or an IRQ-accept rescan re-delivers a still-latched edge (the Mars
        // interrupt-controller flip-flop semantics, from the Emu reference).
        // The SH-2s take the Mars sources as IRL AUTO-VECTORED interrupts
        // (SH7604 INTC, VECMD=0): vector = 64 + the level pair, so VRES/V/H/
        // CMD/PWM land on $47/$46/$45/$44/$43. Retail code installs its real
        // dispatcher at those slots and leaves SDK stubs at the old "software"
        // $44-$4A list -- delivering PWM at $4A hits a stub RTE and the sound
        // engine never runs.
        static constexpr std::uint8_t irq_vint = 0x01U; // V-blank: level 12, vector 0x46
        static constexpr std::uint8_t irq_hint = 0x02U; // H-blank: level 10, vector 0x45
        static constexpr std::uint8_t irq_cmd = 0x04U;  // 68K cmd: level 8,  vector 0x44
        static constexpr std::uint8_t irq_pwm = 0x08U;  // PWM:     level 6,  vector 0x43
        std::uint8_t master_irq_mask{};
        std::uint8_t slave_irq_mask{};
        std::uint8_t master_irq_latch{};
        std::uint8_t slave_irq_latch{};

        // PWM unit. CNTL/CYCLE are shared between the 68000 and SH-2 views; the
        // L/R FIFOs are three entries deep (a push into a full FIFO is rejected,
        // matching hardware). The unit steps once every CYCLE SH-2 cycles: each
        // step pops one duty value per channel into the DAC (an empty FIFO holds
        // the previous value -- the carrier keeps running), converts duty to
        // signed PCM centred on CYCLE/2, and advances the interrupt counter; when
        // the counter reaches the CNTL TM field the PWM interrupt latches.
        std::uint16_t pwm_cntl{};
        std::uint16_t pwm_cycle{};
        // DC-blocker state (per channel): the hardware PWM output is
        // AC-coupled into the mix, so a held duty decays to silence.
        float pwm_dcb_x_l{}, pwm_dcb_y_l{};
        float pwm_dcb_x_r{}, pwm_dcb_y_r{};
        std::array<std::uint16_t, pwm_fifo_depth> pwm_fifo_l{};
        std::array<std::uint16_t, pwm_fifo_depth> pwm_fifo_r{};
        std::uint8_t pwm_fifo_l_count{};
        std::uint8_t pwm_fifo_r_count{};
        std::uint16_t pwm_current_l{}; // last duty latched into each DAC
        std::uint16_t pwm_current_r{};
        std::int16_t pwm_audio_l{}; // converted PCM output
        std::int16_t pwm_audio_r{};
        std::uint64_t pwm_cycle_acc{};     // SH-2 cycles toward the next step
        std::uint8_t pwm_irq_step_count{}; // steps toward the TM threshold
        std::uint8_t pwm_lch_high{};       // byte-pair latches for FIFO pushes
        std::uint8_t pwm_rch_high{};
        std::uint8_t pwm_mono_high{};

        // Advance the PWM unit by the SH-2 cycles just executed (driven from
        // run_cycles; CYCLE = 0 disables stepping).
        void step_pwm(std::uint64_t sh2_cycles);
        [[nodiscard]] std::int16_t pwm_output_l() const noexcept { return pwm_audio_l; }
        [[nodiscard]] std::int16_t pwm_output_r() const noexcept { return pwm_audio_r; }

        // Real-time PWM audio sink (the sn76489/ym2612 capture model): when
        // enabled, each PWM step queues one interleaved L/R pair; the host
        // drains and resamples (native rate = SH-2 clock / CYCLE).
        void enable_pwm_capture(bool on) noexcept { pwm_capture = on; }
        [[nodiscard]] std::size_t pwm_pending_samples() const noexcept {
            return pwm_queue.size() / 2U;
        }
        std::size_t drain_pwm_samples(std::int16_t* out, std::size_t max_frames) noexcept;
        bool pwm_capture{};
        std::vector<std::int16_t> pwm_queue{};

        // DREQ -- the 68000-to-SH-2 DMA stream. The 68000 arms it through the
        // control register (bit 2 = 68S) and writes words into the FIFO port;
        // the FIFO asserts the SH-2 DMAC's module-request DREQ level while
        // words are queued, and the SH-2-side FIFO read port pops them (the
        // DMAC's source reads land there). Clearing 68S flushes the queue.
        // src/dst/len are CPU-visible storage (carts use them as a contract
        // between their own 68000 and SH-2 code).
        static constexpr std::size_t dreq_fifo_depth = 8U;
        std::uint16_t dreq_ctrl{}; // bit 2 = 68S; bits 1:0 latched (RV/DMA)
        std::uint16_t dreq_src_hi{};
        std::uint16_t dreq_src_lo{};
        std::uint16_t dreq_dst_hi{};
        std::uint16_t dreq_dst_lo{};
        std::uint16_t dreq_len{};
        std::array<std::uint16_t, dreq_fifo_depth> dreq_fifo{};
        std::uint8_t dreq_fifo_count{};
        std::uint8_t dreq_read_high{};  // SH-2-side byte-pair pop latch
        std::uint8_t dreq_write_high{}; // 68000-side byte-pair push latch
        void dreq_fifo_push(std::uint16_t word) noexcept;
        [[nodiscard]] std::uint16_t dreq_fifo_pop() noexcept;
        [[nodiscard]] bool dreq_pending() const noexcept { return dreq_fifo_count != 0U; }

        // 68000-side adapter state owned here so the SH-2 register window can
        // act on it. cart_bank: the $A15104 bank-set value (bits 1:0) for the
        // 68000's $900000 window. intm/ints: the $A15102 CMD-interrupt control
        // bits -- set by a 68000 write, held until the TARGET SH-2 writes its
        // CMD-interrupt-clear register ($401A).
        std::uint8_t cart_bank{};
        bool intm_pending{};
        bool ints_pending{};

        // SFX-stream debug counters (read by the player probe; wrap is fine).
        std::uint32_t dbg_dreq_pushes{}; // 68000 words accepted into the DREQ FIFO
        std::uint32_t dbg_dreq_pops{};   // slave-side FIFO pops ($12 odd-byte reads)
        std::uint32_t dbg_pwm_writes{};  // SH-2 byte pairs completing a PWM FIFO push
        std::uint32_t dbg_pwm_irqs{};    // PWM TM interrupts raised

        // COMM bank byte access (big-endian: even byte = high half of the word).
        // Shared by both SH-2s; the Genesis 68000 joins it in the bridge phase.
        [[nodiscard]] std::uint8_t comm_read(std::uint32_t offset) const noexcept;
        void comm_write(std::uint32_t offset, std::uint8_t value) noexcept;

        // SH-2-side system-register window ($00004000, offset 0..0xFF): adapter
        // control ($00/$01), the self-referential interrupt-enable register ($03
        // sets the executing CPU's own mask), and the COMM bank ($20-$2F). PWM /
        // VDP / DMA-FIFO registers are stubbed until their phases. `is_master`
        // selects which CPU's view this access belongs to.
        [[nodiscard]] std::uint8_t sys_reg_read(std::uint32_t offset, bool is_master) noexcept;
        void sys_reg_write(std::uint32_t offset, std::uint8_t value, bool is_master);

        // The second system-register block at $40000000 (the reference maps it
        // alongside the GBR window; retail code reaches the same registers
        // through it). Layout differs from the GBR window: standard big-endian
        // byte lanes on adapter control, and the self-referential
        // interrupt-enable register at +$04 rather than +$02. COMM and PWM
        // share the GBR-window layout.
        [[nodiscard]] std::uint8_t alt_reg_read(std::uint32_t offset, bool is_master) noexcept;
        void alt_reg_write(std::uint32_t offset, std::uint8_t value, bool is_master);

        // 32X VDP register window ($00004100, offset 0..0xFF; both CPUs see the
        // same chip) and palette CRAM ($00004200, offset 0..0x1FF). Byte access
        // promotes to read-modify-write on the 16-bit cell; an AUTOFILL_DATA
        // write fires the autofill into the shared frame buffer. The 68000's
        // $A15180 window routes through the same pair.
        [[nodiscard]] std::uint8_t vdp_reg_read(std::uint32_t offset) const noexcept;
        void vdp_reg_write(std::uint32_t offset, std::uint8_t value);
        [[nodiscard]] std::uint8_t vdp_pal_read(std::uint32_t offset) const noexcept;
        void vdp_pal_write(std::uint32_t offset, std::uint8_t value);

        // Raise a 32X interrupt source (latches on both CPUs; delivered to each
        // whose mask enables it). No-op while the SH-2s are held in reset.
        void raise_vint();
        void raise_hint();
        void raise_cmd();
        void raise_pwm();
        // Targeted CMD delivery: the Mars system controller routes a CMD asserted
        // through the 68000-side interrupt-control registers to one SH-2 only.
        // Still edge-latched -- a masked edge survives for later re-delivery.
        void raise_cmd_master();
        void raise_cmd_slave();
        // Set a CPU's interrupt-enable mask, then re-deliver any latched edge.
        void set_master_irq_mask(std::uint8_t mask);
        void set_slave_irq_mask(std::uint8_t mask);

        // IRQ-accept rescan entry points (wired to each SH-2's accept callback):
        // re-deliver a latched lower-priority edge once the CPU takes a higher one.
        void master_irq_accept() { fire_latched(master_cpu, master_irq_mask, master_irq_latch); }
        void slave_irq_accept() { fire_latched(slave_cpu, slave_irq_mask, slave_irq_latch); }

        // Map the cartridge ROM into both SH-2 buses (partition-0 window at
        // $02000000 plus the cache-through views at $20000000/$22000000 and
        // their partition aliases). Call once, before the SH-2s are released.
        void attach_cart(std::span<const std::uint8_t> rom);

        // Retarget the $04000000 frame-buffer windows (all partition bases,
        // both buses) at the given 128 KiB bank -- the FS access-bank flip,
        // driven at each V-blank frame-select commit. No-op when unchanged.
        void set_fb_access_bank(int bank);
        int fb_access_bank{0};

        // Frame-buffer access through the live access bank, for the windows
        // that cannot be plain RAM maps: a byte read, a plain byte write, and
        // the overwrite-image write (a zero byte is skipped -- the hardware
        // transparency blit at FB+$20000 and the 68000's $860000 window).
        [[nodiscard]] std::uint8_t fb_read(std::uint32_t offset) const noexcept {
            return framebuffer[static_cast<std::size_t>(fb_access_bank) * fb_bank_size +
                               (offset & (fb_bank_size - 1U))];
        }
        void fb_write(std::uint32_t offset, std::uint8_t value) noexcept {
            framebuffer[static_cast<std::size_t>(fb_access_bank) * fb_bank_size +
                        (offset & (fb_bank_size - 1U))] = value;
        }
        void fb_overwrite_write(std::uint32_t offset, std::uint8_t value) noexcept {
            if (value != 0U) {
                fb_write(offset, value);
            }
        }

        // Power-on: clear board state and (re)load both SH-2 reset vectors from
        // their BIOS. The CPUs stay held until set_sh2_reset(false).
        void reset();
        // Hold (true) or release (false) the two SH-2s. A release edge restarts
        // both CPUs from their BIOS reset vectors (the /RES pin behaviour).
        void set_sh2_reset(bool asserted);
        // Advance both SH-2s when not held in reset (lockstep; real interleaving
        // is a phase-D scheduling concern).
        void run_cycles(std::uint64_t cycles);

      private:
        // Latch a source on both CPUs and deliver it to each whose mask enables it
        // and whose pending level it outranks.
        void deliver_irq(std::uint8_t bit, int level, std::uint8_t vector);
        // Re-scan a CPU's latched sources (priority order) and present the highest
        // unmasked one that outranks its pending slot. Run after a mask write or an
        // IRQ accept.
        void fire_latched(chips::cpu::sh2& cpu, std::uint8_t mask, std::uint8_t& latch);
    };

    std::unique_ptr<sega32x_system> assemble_sega32x();

} // namespace mnemos::manifests::sega32x
