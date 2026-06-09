#include "sega32x_system.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace mnemos::manifests::sega32x {

    namespace {
        // Opt-in COMM-write tracer (MNEMOS_32X_COMM_TRACE=1): one stderr line
        // per SH-2-side COMM write, for boot-handshake debugging.
        [[nodiscard]] bool comm_trace_enabled() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in debug knob
#endif
            static const bool enabled = [] {
                const char* v = std::getenv("MNEMOS_32X_COMM_TRACE");
                return v != nullptr && v[0] != '\0' && v[0] != '0';
            }();
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return enabled;
        }
        // PWM FIFO primitives. A push into a full FIFO drops the value (the
        // hardware rejects the write); a pop from an empty FIFO returns the
        // held DAC value so the carrier keeps running on the last duty.
        void pwm_fifo_push(std::array<std::uint16_t, pwm_fifo_depth>& fifo, std::uint8_t& count,
                           std::uint16_t duty) noexcept {
            if (count >= pwm_fifo_depth) {
                return;
            }
            fifo[count++] = duty & 0x0FFFU;
        }

        [[nodiscard]] std::uint16_t pwm_fifo_pop(std::array<std::uint16_t, pwm_fifo_depth>& fifo,
                                                 std::uint8_t& count, std::uint16_t held) noexcept {
            if (count == 0U) {
                return held;
            }
            const std::uint16_t v = fifo[0];
            for (std::size_t i = 0; i + 1U < pwm_fifo_depth; ++i) {
                fifo[i] = fifo[i + 1U];
            }
            --count;
            return v;
        }

        // FIFO status word: bit 15 = FULL, bit 14 = EMPTY, rest open bus.
        [[nodiscard]] std::uint16_t pwm_fifo_status(std::uint8_t count) noexcept {
            std::uint16_t w = 0U;
            if (count >= pwm_fifo_depth) {
                w |= 0x8000U;
            }
            if (count == 0U) {
                w |= 0x4000U;
            }
            return w;
        }

        // Duty -> signed PCM: the analog filter centres on CYCLE/2, so the
        // audible swing is (duty - CYCLE/2) scaled to the int16 range.
        [[nodiscard]] std::int16_t pwm_duty_to_pcm(std::uint16_t duty, int cycle) noexcept {
            const int half = cycle / 2;
            if (half <= 0) {
                return 0;
            }
            int sample = ((static_cast<int>(duty) - half) * 32767) / half;
            if (sample > 32767) {
                sample = 32767;
            }
            if (sample < -32768) {
                sample = -32768;
            }
            return static_cast<std::int16_t>(sample);
        }

        // The SH-2-side interrupt-enable register (low byte of the $4000 system
        // word) uses the hardware bit order V=8 / H=4 / CMD=2 / PWM=1, the
        // reverse of the internal source bits (VINT=1 .. PWM=8). Translate at
        // the register boundary so the delivery logic keeps one convention.
        [[nodiscard]] std::uint8_t hw_mask_to_internal(std::uint8_t hw) noexcept {
            std::uint8_t m = 0;
            if ((hw & 0x08U) != 0U) {
                m |= sega32x_system::irq_vint;
            }
            if ((hw & 0x04U) != 0U) {
                m |= sega32x_system::irq_hint;
            }
            if ((hw & 0x02U) != 0U) {
                m |= sega32x_system::irq_cmd;
            }
            if ((hw & 0x01U) != 0U) {
                m |= sega32x_system::irq_pwm;
            }
            return m;
        }

        [[nodiscard]] std::uint8_t internal_mask_to_hw(std::uint8_t m) noexcept {
            std::uint8_t hw = 0;
            if ((m & sega32x_system::irq_vint) != 0U) {
                hw |= 0x08U;
            }
            if ((m & sega32x_system::irq_hint) != 0U) {
                hw |= 0x04U;
            }
            if ((m & sega32x_system::irq_cmd) != 0U) {
                hw |= 0x02U;
            }
            if ((m & sega32x_system::irq_pwm) != 0U) {
                hw |= 0x01U;
            }
            return hw;
        }

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

    void sega32x_system::dreq_fifo_push(std::uint16_t word) noexcept {
        if (dreq_fifo_count >= dreq_fifo_depth) {
            return; // full: the word is lost (the 68000 should poll FULL first)
        }
        dreq_fifo[dreq_fifo_count++] = word;
    }

    std::uint16_t sega32x_system::dreq_fifo_pop() noexcept {
        if (dreq_fifo_count == 0U) {
            return 0U;
        }
        const std::uint16_t word = dreq_fifo[0];
        for (std::size_t i = 0; i + 1U < dreq_fifo_depth; ++i) {
            dreq_fifo[i] = dreq_fifo[i + 1U];
        }
        --dreq_fifo_count;
        return word;
    }

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
        if (comm_trace_enabled()) {
            std::fprintf(stderr, "[comm] sh2 wr idx%u <= %04X (mcyc=%llu)\n",
                         static_cast<unsigned>((offset >> 1U) & (comm_words - 1U)), word,
                         static_cast<unsigned long long>(master_cpu.elapsed_cycles()));
        }
    }

    std::uint8_t sega32x_system::sys_reg_read(std::uint32_t offset, bool is_master) noexcept {
        if (offset < 0x02U) {
            // The SH-2-side $4000 system word. Even (high) byte: the adapter
            // flag bits the boot ROMs check -- ADEN (bit 1), FM (bit 7) -- plus
            // the CPU-ID pin in bit 0 (master = 0, slave = 1). Odd (low) byte:
            // the executing CPU's own interrupt-enable mask in the hardware bit
            // order (V=8 / H=4 / CMD=2 / PWM=1) -- self-referential, each SH-2
            // sees only its own.
            if ((offset & 1U) != 0U) {
                return internal_mask_to_hw(is_master ? master_irq_mask : slave_irq_mask);
            }
            return static_cast<std::uint8_t>((adapter_ctrl & 0xFFU) | (is_master ? 0x00U : 0x01U));
        }
        if (offset < 0x04U) {
            // H-interrupt line-count register: round-trip storage (the HINT
            // pacing itself follows the Genesis VDP's line counter).
            if ((offset & 1U) != 0U) {
                return static_cast<std::uint8_t>(hcount);
            }
            return static_cast<std::uint8_t>(hcount >> 8U);
        }
        if (offset >= 0x06U && offset < 0x12U) {
            // DREQ control + src/dst/len (68000-armed storage).
            std::uint16_t w = 0U;
            switch (offset & ~1U) {
            case 0x06U:
                w = static_cast<std::uint16_t>(
                    (dreq_ctrl & 0x0007U) |
                    (dreq_fifo_count >= dreq_fifo_depth ? 0x0080U : 0U)); // FULL
                break;
            case 0x08U:
                w = dreq_src_hi;
                break;
            case 0x0AU:
                w = dreq_src_lo;
                break;
            case 0x0CU:
                w = dreq_dst_hi;
                break;
            case 0x0EU:
                w = dreq_dst_lo;
                break;
            default:
                w = dreq_len;
                break;
            }
            return (offset & 1U) != 0U ? static_cast<std::uint8_t>(w)
                                       : static_cast<std::uint8_t>(w >> 8U);
        }
        if ((offset & ~1U) == 0x12U) {
            // The FIFO read port: the even byte serves the front word's high
            // half; the odd byte completes the word and pops it (the SH-2
            // DMAC's byte-decomposed source reads land here in order).
            if ((offset & 1U) == 0U) {
                const std::uint16_t front = dreq_fifo_count != 0U ? dreq_fifo[0] : 0U;
                dreq_read_high = static_cast<std::uint8_t>(front >> 8U);
                return dreq_read_high;
            }
            return static_cast<std::uint8_t>(dreq_fifo_pop());
        }
        if (offset >= comm_offset && offset < comm_offset + comm_words * 2U) {
            return comm_read(offset - comm_offset);
        }
        // PWM registers at $30-$39 (CNTL/CYCLE shared with the 68000's $A15130
        // view; LCH/RCH/MONO reads return FIFO status, MONO ORs both channels).
        if (offset >= 0x30U && offset < 0x3AU) {
            std::uint16_t w = 0U;
            switch (offset & ~1U) {
            case 0x30U:
                w = pwm_cntl;
                break;
            case 0x32U:
                w = pwm_cycle;
                break;
            case 0x34U:
                w = pwm_fifo_status(pwm_fifo_l_count);
                break;
            case 0x36U:
                w = pwm_fifo_status(pwm_fifo_r_count);
                break;
            case 0x38U:
                w = static_cast<std::uint16_t>(pwm_fifo_status(pwm_fifo_l_count) |
                                               pwm_fifo_status(pwm_fifo_r_count));
                // MONO EMPTY only when both FIFOs are empty.
                if (pwm_fifo_l_count != 0U || pwm_fifo_r_count != 0U) {
                    w &= static_cast<std::uint16_t>(~0x4000U);
                }
                break;
            default:
                break;
            }
            return (offset & 1U) != 0U ? static_cast<std::uint8_t>(w)
                                       : static_cast<std::uint8_t>(w >> 8U);
        }
        return 0U;
    }

    void sega32x_system::sys_reg_write(std::uint32_t offset, std::uint8_t value, bool is_master) {
        if (comm_trace_enabled() && offset < 0x10U) {
            std::fprintf(stderr, "[sysreg] %s wr off=%02X <= %02X\n", is_master ? "m" : "s",
                         static_cast<unsigned>(offset), value);
        }
        if (offset < 0x02U) {
            // Even byte: the adapter flag byte (FM and friends; ADEN/CART are
            // read-only inputs). Odd byte: the executing CPU's own
            // interrupt-enable mask, hardware bit order.
            if ((offset & 1U) != 0U) {
                if (is_master) {
                    set_master_irq_mask(hw_mask_to_internal(value));
                } else {
                    set_slave_irq_mask(hw_mask_to_internal(value));
                }
            } else {
                adapter_ctrl = static_cast<std::uint16_t>((adapter_ctrl & 0xFF00U) | value);
            }
            return;
        }
        if (offset < 0x04U) {
            // H-interrupt line-count register: storage only.
            if ((offset & 1U) != 0U) {
                hcount = static_cast<std::uint16_t>((hcount & 0xFF00U) | value);
            } else {
                hcount = static_cast<std::uint16_t>((hcount & 0x00FFU) |
                                                    (static_cast<std::uint16_t>(value) << 8U));
            }
            return;
        }
        if (offset >= comm_offset && offset < comm_offset + comm_words * 2U) {
            comm_write(offset - comm_offset, value);
            return;
        }
        const auto write_word_lane = [offset, value](std::uint16_t& word) {
            if ((offset & 1U) != 0U) {
                word = static_cast<std::uint16_t>((word & 0xFF00U) | value);
            } else {
                word = static_cast<std::uint16_t>((word & 0x00FFU) |
                                                  (static_cast<std::uint16_t>(value) << 8U));
            }
        };
        if ((offset & ~1U) == 0x30U) {
            write_word_lane(pwm_cntl);
            return;
        }
        if ((offset & ~1U) == 0x32U) {
            write_word_lane(pwm_cycle);
            return;
        }
        // PWM FIFO pushes: the even byte latches the duty's high half, the odd
        // byte completes the 12-bit value and pushes. MONO pushes both FIFOs.
        const auto fifo_write = [this, offset, value](std::uint8_t& high_latch, bool left,
                                                      bool right) {
            if ((offset & 1U) == 0U) {
                high_latch = value;
                return;
            }
            const auto duty =
                static_cast<std::uint16_t>((static_cast<std::uint16_t>(high_latch) << 8U) | value);
            if (left) {
                pwm_fifo_push(pwm_fifo_l, pwm_fifo_l_count, duty);
            }
            if (right) {
                pwm_fifo_push(pwm_fifo_r, pwm_fifo_r_count, duty);
            }
        };
        if ((offset & ~1U) == 0x34U) {
            fifo_write(pwm_lch_high, true, false);
            return;
        }
        if ((offset & ~1U) == 0x36U) {
            fifo_write(pwm_rch_high, false, true);
            return;
        }
        if ((offset & ~1U) == 0x38U) {
            fifo_write(pwm_mono_high, true, true);
            return;
        }
        // else: VDP / DMA-FIFO -- not yet modelled
    }

    std::uint8_t sega32x_system::alt_reg_read(std::uint32_t offset, bool is_master) noexcept {
        if (offset < 0x02U) {
            // Standard big-endian lanes in this window: even = high byte.
            return (offset & 1U) != 0U ? static_cast<std::uint8_t>(adapter_ctrl)
                                       : static_cast<std::uint8_t>(adapter_ctrl >> 8U);
        }
        if ((offset & ~1U) == 0x04U) {
            // The self-referential interrupt-enable register sits at +$04 in
            // this block, hardware bit order like the $4000 low byte.
            if ((offset & 1U) != 0U) {
                return internal_mask_to_hw(is_master ? master_irq_mask : slave_irq_mask);
            }
            return 0U;
        }
        if (offset >= comm_offset) {
            return sys_reg_read(offset, is_master); // COMM + PWM: shared layout
        }
        return 0U;
    }

    void sega32x_system::alt_reg_write(std::uint32_t offset, std::uint8_t value, bool is_master) {
        if (comm_trace_enabled() && offset < 0x10U) {
            std::fprintf(stderr, "[altreg] %s wr off=%02X <= %02X\n", is_master ? "m" : "s",
                         static_cast<unsigned>(offset), value);
        }
        if (offset < 0x02U) {
            if ((offset & 1U) != 0U) {
                adapter_ctrl = static_cast<std::uint16_t>((adapter_ctrl & 0xFF00U) | value);
            } else {
                adapter_ctrl = static_cast<std::uint16_t>(
                    (adapter_ctrl & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
            }
            return;
        }
        if ((offset & ~1U) == 0x04U) {
            if ((offset & 1U) != 0U) {
                if (is_master) {
                    set_master_irq_mask(hw_mask_to_internal(value));
                } else {
                    set_slave_irq_mask(hw_mask_to_internal(value));
                }
            }
            return;
        }
        if (offset >= comm_offset) {
            sys_reg_write(offset, value, is_master); // COMM + PWM: shared layout
        }
    }

    std::uint8_t sega32x_system::vdp_reg_read(std::uint32_t offset) const noexcept {
        const std::uint16_t word = vdp.read16(offset & 0xFEU);
        return (offset & 1U) != 0U ? static_cast<std::uint8_t>(word)
                                   : static_cast<std::uint8_t>(word >> 8U);
    }

    void sega32x_system::vdp_reg_write(std::uint32_t offset, std::uint8_t value) {
        const std::uint32_t reg = offset & 0xFEU;
        const std::uint16_t cur = vdp.read16(reg);
        const std::uint16_t next =
            (offset & 1U) != 0U ? static_cast<std::uint16_t>((cur & 0xFF00U) | value)
                                : static_cast<std::uint16_t>(
                                      (cur & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
        vdp.write16(reg, next);
        // A word write to the first AUTOFILL_DATA cell fires the fill into the
        // ACCESS bank of the frame buffer (the chip itself only latches). On
        // this byte-decomposed bus the word completes with its LOW byte --
        // firing on both halves would run a second fill from the latched end
        // address.
        if (reg == chips::video::sega32x_vdp::reg_autofill_data && (offset & 1U) != 0U) {
            vdp.autofill_execute(
                {framebuffer.data() + static_cast<std::size_t>(vdp.access_bank()) * fb_bank_size,
                 fb_bank_size});
        }
        // An FBCR write during V-blank commits the frame-select immediately --
        // swing the $04000000 window onto the (possibly new) access bank.
        if (reg == chips::video::sega32x_vdp::reg_fb_control) {
            set_fb_access_bank(vdp.access_bank());
        }
    }

    std::uint8_t sega32x_system::vdp_pal_read(std::uint32_t offset) const noexcept {
        const std::uint16_t word = vdp.palette_read16(offset & 0x1FEU);
        return (offset & 1U) != 0U ? static_cast<std::uint8_t>(word)
                                   : static_cast<std::uint8_t>(word >> 8U);
    }

    void sega32x_system::vdp_pal_write(std::uint32_t offset, std::uint8_t value) {
        const std::uint32_t cell = offset & 0x1FEU;
        const std::uint16_t cur = vdp.palette_read16(cell);
        const std::uint16_t next =
            (offset & 1U) != 0U ? static_cast<std::uint16_t>((cur & 0xFF00U) | value)
                                : static_cast<std::uint16_t>(
                                      (cur & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
        vdp.palette_write16(cell, next);
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

    namespace {
        // Targeted CMD edge for one CPU: latch regardless of mask, deliver only if
        // the mask enables it and level 8 outranks the CPU's pending slot.
        void deliver_cmd_to(bool reset_asserted, mnemos::chips::cpu::sh2& cpu, std::uint8_t mask,
                            std::uint8_t& latch) {
            if (reset_asserted) {
                return;
            }
            latch |= sega32x_system::irq_cmd;
            if ((mask & sega32x_system::irq_cmd) == 0U || cpu.pending_irq_level() >= 8) {
                return;
            }
            cpu.set_irq(8, 0x48U);
            latch &= static_cast<std::uint8_t>(~sega32x_system::irq_cmd);
        }
    } // namespace

    void sega32x_system::raise_cmd_master() {
        deliver_cmd_to(sh2_reset_asserted, master_cpu, master_irq_mask, master_irq_latch);
    }

    void sega32x_system::raise_cmd_slave() {
        deliver_cmd_to(sh2_reset_asserted, slave_cpu, slave_irq_mask, slave_irq_latch);
    }

    void sega32x_system::set_master_irq_mask(std::uint8_t mask) {
        master_irq_mask = mask;
        fire_latched(master_cpu, master_irq_mask, master_irq_latch);
    }

    void sega32x_system::set_slave_irq_mask(std::uint8_t mask) {
        slave_irq_mask = mask;
        fire_latched(slave_cpu, slave_irq_mask, slave_irq_latch);
    }

    void sega32x_system::attach_cart(std::span<const std::uint8_t> rom) {
        cart_rom = rom.first(std::min<std::size_t>(rom.size(), cart_window_size));
        for (topology::bus* bus : {&master_bus, &slave_bus}) {
            // Partition-0 family: cart window at +$02000000.
            for (const std::uint32_t base : p0_bases) {
                bus->map_rom(base + cart_base, cart_rom, 0);
            }
            // Cache-through family: cart at the partition base (the boot ROM is
            // a partition-0 resident) plus the +$02000000 header-check alias.
            // The system-register window at +$4000 stays on top (priority 1).
            for (const std::uint32_t base : p1_bases) {
                bus->map_rom(base, cart_rom, 0);
                bus->map_rom(base + cart_base, cart_rom, 0);
            }
        }
    }

    void sega32x_system::set_fb_access_bank(int bank) {
        bank &= 1;
        if (bank == fb_access_bank) {
            return;
        }
        fb_access_bank = bank;
        const std::span<std::uint8_t> view{
            framebuffer.data() + static_cast<std::size_t>(bank) * fb_bank_size, fb_bank_size};
        for (topology::bus* bus : {&master_bus, &slave_bus}) {
            for (const std::uint32_t base : p0_bases) {
                bus->retarget_ram(base + framebuffer_base, view);
            }
            for (const std::uint32_t base : p1_bases) {
                bus->retarget_ram(base + framebuffer_base, view);
            }
        }
    }

    void sega32x_system::set_sh2_reset(bool asserted) {
        // The adapter-control RES bit drives the /RES pin directly: a release
        // edge restarts both CPUs from their BIOS reset vectors. Re-releasing a
        // running pair is a no-op; holding parks them where they are.
        if (!asserted && sh2_reset_asserted) {
            // Reference-derived boot workaround: pre-seed "_CD_" in COMM 0/1 so
            // the slave boot ROM exits its wait loop immediately and reaches the
            // slave game entry before the master game code's handshake wait.
            // The master boot ROM overwrites this with M_OK when it completes.
            comm[0] = 0x5F43U;
            comm[1] = 0x445FU;
            master_cpu.reset(chips::reset_kind::power_on);
            slave_cpu.reset(chips::reset_kind::power_on);
        }
        sh2_reset_asserted = asserted;
    }

    void sega32x_system::reset() {
        sh2_reset_asserted = true;
        adapter_ctrl = 0U;
        hcount = 0U;
        comm.fill(0U);
        master_irq_mask = 0U;
        slave_irq_mask = 0U;
        master_irq_latch = 0U;
        slave_irq_latch = 0U;
        m_ok_high_seen = false;
        dreq_ctrl = 0U;
        dreq_src_hi = 0U;
        dreq_src_lo = 0U;
        dreq_dst_hi = 0U;
        dreq_dst_lo = 0U;
        dreq_len = 0U;
        dreq_fifo.fill(0U);
        dreq_fifo_count = 0U;
        dreq_read_high = 0U;
        dreq_write_high = 0U;
        pwm_cntl = 0U;
        pwm_cycle = 0U;
        pwm_fifo_l.fill(0U);
        pwm_fifo_r.fill(0U);
        pwm_fifo_l_count = 0U;
        pwm_fifo_r_count = 0U;
        pwm_current_l = 0U;
        pwm_current_r = 0U;
        pwm_audio_l = 0;
        pwm_audio_r = 0;
        pwm_cycle_acc = 0U;
        pwm_irq_step_count = 0U;
        pwm_lch_high = 0U;
        pwm_rch_high = 0U;
        pwm_mono_high = 0U;
        vdp.reset(chips::reset_kind::power_on);
        // The buses are already attached, so reset reads each CPU's PC/SP from its
        // own BIOS reset vectors at $0.
        master_cpu.reset(chips::reset_kind::power_on);
        slave_cpu.reset(chips::reset_kind::power_on);
    }

    void sega32x_system::step_pwm(std::uint64_t sh2_cycles) {
        const int cycle = pwm_cycle & 0x0FFFU;
        if (cycle == 0) {
            return; // CYCLE = 0 disables stepping
        }
        pwm_cycle_acc += sh2_cycles;
        while (pwm_cycle_acc >= static_cast<std::uint64_t>(cycle)) {
            pwm_cycle_acc -= static_cast<std::uint64_t>(cycle);

            pwm_current_l = pwm_fifo_pop(pwm_fifo_l, pwm_fifo_l_count, pwm_current_l);
            pwm_current_r = pwm_fifo_pop(pwm_fifo_r, pwm_fifo_r_count, pwm_current_r);
            pwm_audio_l = pwm_duty_to_pcm(pwm_current_l, cycle);
            pwm_audio_r = pwm_duty_to_pcm(pwm_current_r, cycle);
            if (pwm_capture) {
                pwm_queue.push_back(pwm_audio_l);
                pwm_queue.push_back(pwm_audio_r);
            }

            // CNTL bits 11-8 = TM, the interrupt-rate divider: every TM steps
            // the PWM interrupt latches on both SH-2s (per-CPU masks gate
            // delivery as usual).
            const auto tm = static_cast<std::uint8_t>((pwm_cntl >> 8U) & 0x0FU);
            if (tm > 0U) {
                ++pwm_irq_step_count;
                if (pwm_irq_step_count >= tm) {
                    pwm_irq_step_count = 0U;
                    raise_pwm();
                }
            }
        }
    }

    std::size_t sega32x_system::drain_pwm_samples(std::int16_t* out,
                                                  std::size_t max_frames) noexcept {
        const std::size_t frames = std::min(max_frames, pwm_queue.size() / 2U);
        if (out != nullptr && frames > 0U) {
            std::copy_n(pwm_queue.begin(), frames * 2U, out);
        }
        pwm_queue.erase(pwm_queue.begin(),
                        pwm_queue.begin() + static_cast<std::ptrdiff_t>(frames * 2U));
        return frames;
    }

    void sega32x_system::run_cycles(std::uint64_t cycles) {
        if (sh2_reset_asserted) {
            return; // both SH-2s held inactive
        }
        const std::uint64_t before = master_cpu.elapsed_cycles();
        master_cpu.tick(cycles);
        slave_cpu.tick(cycles);
        // The PWM unit advances on the SH-2 clock; drive it with the master's
        // actual progress (instruction-atomic stepping can overshoot `cycles`).
        step_pwm(master_cpu.elapsed_cycles() - before);
    }

    std::unique_ptr<sega32x_system> assemble_sega32x() {
        auto sys = std::make_unique<sega32x_system>();
        auto* s = sys.get();

        // Both CPUs share the SDRAM, frame buffer, and COMM bank but each boots
        // from its own ROM at $0. Every region is mirrored across the partition
        // bases (cached / cache-through / aliases) since no cache is modelled.
        for (const std::uint32_t base : p0_bases) {
            s->master_bus.map_rom(base + bios_base, s->m_bios, 0);
            s->slave_bus.map_rom(base + bios_base, s->s_bios, 0);
        }
        // The $04000000 window exposes ONE 128 KiB frame-buffer bank (the FS
        // access bank, initially bank 0); set_fb_access_bank retargets it at
        // each frame-select commit. The displayed bank is only reachable
        // through the VDP's composition path, like hardware.
        const std::span<std::uint8_t> fb_bank0{s->framebuffer.data(), fb_bank_size};
        for (topology::bus* bus : {&s->master_bus, &s->slave_bus}) {
            for (const std::uint32_t base : p0_bases) {
                bus->map_ram(base + framebuffer_base, fb_bank0, 0);
                bus->map_ram(base + sdram_base, s->sdram, 0);
            }
            for (const std::uint32_t base : p1_bases) {
                bus->map_ram(base + framebuffer_base, fb_bank0, 0);
                bus->map_ram(base + sdram_base, s->sdram, 0);
            }
        }
        // SH-2-side system registers at +$4000 in every partition, per CPU:
        // adapter control, the self-referential interrupt-enable register, and
        // the shared COMM bank. Priority 1 keeps the window above the
        // cache-through cart view that spans the partition-1 bases.
        const auto map_sysregs = [s](topology::bus& bus, bool is_master) {
            const auto map_at = [&bus, s, is_master](std::uint32_t base) {
                bus.map_mmio(
                    base, sysreg_size,
                    [s, base, is_master](std::uint32_t a) {
                        return s->sys_reg_read(a - base, is_master);
                    },
                    [s, base, is_master](std::uint32_t a, std::uint8_t v) {
                        s->sys_reg_write(a - base, v, is_master);
                    },
                    1);
            };
            for (const std::uint32_t base : p0_bases) {
                map_at(base + sysreg_base);
            }
            for (const std::uint32_t base : p1_bases) {
                map_at(base + sysreg_base);
            }
        };
        map_sysregs(s->master_bus, true);
        map_sysregs(s->slave_bus, false);

        // The second system-register block at $40000000 (alt layout: the
        // interrupt-enable register at +$04), with the VDP registers at +$100
        // and the palette at +$200 -- retail code reaches the same hardware
        // through both this block and the $4000 windows.
        const auto map_alt_regs = [s](topology::bus& bus, bool is_master) {
            bus.map_mmio(
                0x40000000U, sysreg_size,
                [s, is_master](std::uint32_t a) {
                    return s->alt_reg_read(a - 0x40000000U, is_master);
                },
                [s, is_master](std::uint32_t a, std::uint8_t v) {
                    s->alt_reg_write(a - 0x40000000U, v, is_master);
                },
                1);
            bus.map_mmio(
                0x40000100U, vdp_reg_size,
                [s](std::uint32_t a) { return s->vdp_reg_read(a - 0x40000100U); },
                [s](std::uint32_t a, std::uint8_t v) { s->vdp_reg_write(a - 0x40000100U, v); }, 1);
            bus.map_mmio(
                0x40000200U, vdp_pal_size,
                [s](std::uint32_t a) { return s->vdp_pal_read(a - 0x40000200U); },
                [s](std::uint32_t a, std::uint8_t v) { s->vdp_pal_write(a - 0x40000200U, v); }, 1);
        };
        map_alt_regs(s->master_bus, true);
        map_alt_regs(s->slave_bus, false);

        // The 32X VDP register window (+$4100) and palette CRAM (+$4200), shared
        // by both CPUs, at every partition base. Priority 1 over the
        // cache-through cart view, like the system registers.
        for (topology::bus* bus : {&s->master_bus, &s->slave_bus}) {
            const auto map_vdp_at = [bus, s](std::uint32_t base) {
                bus->map_mmio(
                    base + vdp_reg_base, vdp_reg_size,
                    [s, base](std::uint32_t a) { return s->vdp_reg_read(a - base - vdp_reg_base); },
                    [s, base](std::uint32_t a, std::uint8_t v) {
                        s->vdp_reg_write(a - base - vdp_reg_base, v);
                    },
                    1);
                bus->map_mmio(
                    base + vdp_pal_base, vdp_pal_size,
                    [s, base](std::uint32_t a) { return s->vdp_pal_read(a - base - vdp_pal_base); },
                    [s, base](std::uint32_t a, std::uint8_t v) {
                        s->vdp_pal_write(a - base - vdp_pal_base, v);
                    },
                    1);
            };
            for (const std::uint32_t base : p0_bases) {
                map_vdp_at(base);
            }
            for (const std::uint32_t base : p1_bases) {
                map_vdp_at(base);
            }
        }

        s->master_cpu.attach_bus(s->master_bus);
        s->slave_cpu.attach_bus(s->slave_bus);

        // When a CPU accepts an IRQ, re-scan its latched sources so a waiting
        // lower-priority edge is delivered rather than stranded.
        s->master_cpu.set_irq_accept_callback([s](int, std::uint8_t) { s->master_irq_accept(); });
        s->slave_cpu.set_irq_accept_callback([s](int, std::uint8_t) { s->slave_irq_accept(); });

        // Module-request DREQ level for both CPUs' DMACs: asserted while the
        // 68000-to-SH-2 FIFO holds words (the armed channel's source reads pop
        // it through the $4012 port, so the transfer self-regulates).
        s->master_cpu.set_dmac_dreq_query([s](int) { return s->dreq_pending(); });
        s->slave_cpu.set_dmac_dreq_query([s](int) { return s->dreq_pending(); });
        return sys;
    }

} // namespace mnemos::manifests::sega32x
