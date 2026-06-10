#include "sega32x_machine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace mnemos::manifests::sega32x {

    namespace {

        // 68000-side 32X register window, word-level ($A15100 + off). Byte access
        // is promoted to read-modify-write on the 16-bit cell by the bus handlers
        // below, so handshake semantics (reset-bit toggles, COMM byte writes)
        // behave the way retail code expects.
        std::uint16_t m68k_reg_read_word(const sega32x_system& tx, std::uint32_t off) {
            // Adapter control -- $A15100. Bit 15=FM, bit 1=nRES (the live SH-2
            // /RES line: 1 = running), bit 0=ADEN, bit 7 = the V-blank status
            // mirror (the cart's security block spins on it before enabling).
            // The retail security block proves the layout: it writes $01 and
            // immediately runs through the $880000 window (ADEN must be bit 0),
            // then writes $03 to start the SH-2s (nRES is bit 1).
            if (off == 0x00U) {
                std::uint16_t v = static_cast<std::uint16_t>(tx.adapter_ctrl & ~0x0003U);
                if (tx.adapter_enabled) {
                    v |= 0x0001U;
                }
                if (!tx.sh2_reset_asserted) {
                    v |= 0x0002U;
                }
                return v;
            }
            // Interrupt control -- $A15102. Bit 0 = INTM, bit 1 = INTS: the CMD
            // requests to the master / slave SH-2. Each bit reads 1 from the
            // 68000 write until the TARGET SH-2 acknowledges through its
            // CMD-interrupt-clear register ($401A).
            if (off == 0x02U) {
                return static_cast<std::uint16_t>((tx.ints_pending ? 0x2U : 0x0U) |
                                                  (tx.intm_pending ? 0x1U : 0x0U));
            }
            // Bank set -- $A15104. Bits 1:0 pick which 1 MiB of cart the
            // 68000's $900000 window views.
            if (off == 0x04U) {
                return tx.cart_bank;
            }
            // DREQ group: control at $A15106 (bit 2 = 68S, bit 7 = FIFO FULL),
            // src/dst/len storage at $A15108-$A15110, the FIFO write port at
            // $A15112 (write-only; reads return 0).
            if (off == 0x06U) {
                return static_cast<std::uint16_t>(
                    (tx.dreq_ctrl & 0x0007U) |
                    (tx.dreq_fifo_count >= sega32x_system::dreq_fifo_depth ? 0x0080U : 0U));
            }
            if (off == 0x08U) {
                return tx.dreq_src_hi;
            }
            if (off == 0x0AU) {
                return tx.dreq_src_lo;
            }
            if (off == 0x0CU) {
                return tx.dreq_dst_hi;
            }
            if (off == 0x0EU) {
                return tx.dreq_dst_lo;
            }
            if (off == 0x10U) {
                return tx.dreq_len;
            }
            // COMM bank: 8 x 16-bit at $A15120-$A1512F, shared with the SH-2s.
            if (off >= 0x20U && off <= 0x2FU) {
                return tx.comm[(off - 0x20U) >> 1U];
            }
            // PWM scaffold at $A15130-$A15139: CNTL/CYCLE latched, FIFO status
            // reads 0 (not-full) until the PWM phase models the FIFOs.
            if (off == 0x30U) {
                return tx.pwm_cntl;
            }
            if (off == 0x32U) {
                return tx.pwm_cycle;
            }
            return 0U;
        }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv: opt-in debug knob
#endif
        bool reg_trace_enabled() {
            static const bool on = [] {
                const char* p = std::getenv("MNEMOS_32X_REGTRACE");
                return p != nullptr && p[0] != '\0' && p[0] != '0';
            }();
            return on;
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

        void m68k_reg_write_word(sega32x_machine& m, std::uint32_t off, std::uint16_t val) {
            sega32x_system& tx = *m.thirtytwox;
            manifests::genesis::genesis_system* gen = m.genesis.get();
            if (reg_trace_enabled() && off < 0x30U) {
                std::fprintf(stderr, "[reg] 68k w $A151%02X = %04X\n", off, val);
            }
            if (off == 0x00U) {
                // Bit 0 = ADEN: the cart's security block sets it alone first;
                // the 0->1 edge switches the 68000 map (G BIOS vectors overlay
                // $000000) while the SH-2s stay parked. Bit 1 = nRES, driving
                // the SH-2 /RES pin: ADEN+nRES releases (a re-release while
                // running is safe -- the BIOS checks ADEN at its reset vector),
                // nRES=0 always parks. Latched bits (FM and friends) read back;
                // bit 7 stays with the V-blank mirror.
                const bool aden = (val & 0x0001U) != 0U;
                const bool res_release = (val & 0x0002U) != 0U;
                tx.adapter_ctrl =
                    static_cast<std::uint16_t>((val & 0xFF7CU) | (tx.adapter_ctrl & 0x0080U));
                if (aden && !tx.adapter_enabled) {
                    tx.adapter_enabled = true;
                    if (m.g_bios_present) {
                        gen->bus.map_rom(0x000000U, tx.g_bios, 1);
                    }
                }
                if (aden && res_release) {
                    tx.set_sh2_reset(false);
                } else if (!res_release) {
                    tx.set_sh2_reset(true);
                }
                return;
            }
            // Interrupt control -- $A15102: writing 1 to bit 0 (INTM) / bit 1
            // (INTS) asserts the CMD interrupt at the master / slave SH-2
            // (delivery still gated by that SH-2's own CMD enable; undelivered
            // edges stay latched). The bits hold until the target SH-2 writes
            // its CMD-interrupt-clear register -- 68000 writes never clear.
            if (off == 0x02U) {
                if ((val & 0x1U) != 0U) {
                    if (!tx.intm_pending) {
                        tx.raise_cmd_master();
                    }
                    tx.intm_pending = true;
                }
                if ((val & 0x2U) != 0U) {
                    if (!tx.ints_pending) {
                        tx.raise_cmd_slave();
                    }
                    tx.ints_pending = true;
                }
                return;
            }
            // Bank set -- $A15104: bits 1:0 swing the 68000's $900000 window
            // onto that 1 MiB of the cart (the SEGA-logo decompressor in the
            // standard cart startup streams through it). A bank past the end
            // of a smaller cart leaves the window unchanged (open-bus-ish).
            if (off == 0x04U) {
                tx.cart_bank = static_cast<std::uint8_t>(val & 0x3U);
                if (gen != nullptr) {
                    const std::span<const std::uint8_t> rom{gen->rom};
                    const std::size_t window = std::min<std::size_t>(rom.size(), 0x100000U);
                    const std::size_t base = static_cast<std::size_t>(tx.cart_bank) << 20U;
                    if (window > 0U && base + window <= rom.size()) {
                        gen->bus.retarget_rom(0x900000U, rom.subspan(base, window));
                    }
                }
                return;
            }
            if (off == 0x06U) {
                // DREQ control: the 68000 owns 68S (bit 2); dropping it aborts
                // the stream and flushes any queued words.
                tx.dreq_ctrl = val & 0x0007U;
                if ((val & 0x0004U) == 0U) {
                    tx.dreq_fifo_count = 0U;
                }
                return;
            }
            if (off == 0x08U) {
                tx.dreq_src_hi = val & 0x00FFU; // 24-bit address: high byte only
                return;
            }
            if (off == 0x0AU) {
                tx.dreq_src_lo = val & 0xFFFEU; // word-aligned
                return;
            }
            if (off == 0x0CU) {
                tx.dreq_dst_hi = val & 0x00FFU;
                return;
            }
            if (off == 0x0EU) {
                tx.dreq_dst_lo = val & 0xFFFEU;
                return;
            }
            if (off == 0x10U) {
                tx.dreq_len = val;
                return;
            }
            // (The FIFO write port at $12 is handled at byte level in the bus
            // lambda -- a word must push exactly once, not per RMW half.)
            if (off >= 0x20U && off <= 0x2FU) {
                tx.comm[(off - 0x20U) >> 1U] = val;
                return;
            }
            if (off == 0x30U) {
                tx.pwm_cntl = val;
                return;
            }
            if (off == 0x32U) {
                tx.pwm_cycle = val;
                return;
            }
            // DREQ / FIFO / unclaimed offsets: drop until their phases.
        }

    } // namespace

    void sega32x_machine::begin_slice() noexcept {
        slice_base_main_ = genesis->cpu.elapsed_cycles();
        slice_base_sh2_ = thirtytwox->master_cpu.elapsed_cycles();
    }

    void sega32x_machine::catch_up_sh2() {
        // Run both SH-2s up to the 68000's position within the current slice. The
        // SH-2s tick at 3x the 68000, so the target is the slice's 68000 delta
        // scaled by 3. run_cycles is a no-op while the SH-2s are held in reset.
        const std::uint64_t main_now = genesis->cpu.elapsed_cycles();
        if (main_now <= slice_base_main_) {
            return;
        }
        const std::uint64_t main_delta = main_now - slice_base_main_;
        const std::uint64_t target = slice_base_sh2_ + main_delta * sh2_clock_multiplier;
        const std::uint64_t cur = thirtytwox->master_cpu.elapsed_cycles();
        if (target > cur) {
            thirtytwox->run_cycles(target - cur);
        }
    }

    std::unique_ptr<sega32x_machine>
    assemble_sega32x_machine(std::vector<std::uint8_t> cart, const sega32x_bios& bios,
                             const genesis::genesis_config& config) {
        auto machine = std::make_unique<sega32x_machine>();
        machine->thirtytwox = assemble_sega32x();
        // The Genesis main side boots the 32X cartridge as its cartridge ROM.
        machine->genesis = genesis::assemble_genesis(std::move(cart), config);

        sega32x_system* tx = machine->thirtytwox.get();
        genesis::genesis_system* g = machine->genesis.get();
        topology::bus& bus = machine->genesis->bus;
        tx->vdp.set_pal(config.video_region == mnemos::video_region::pal);

        // Load the boot ROM images (each clamped to its canonical size) and give
        // the SH-2s their cart windows. The Genesis owns the cart bytes; both
        // live on the machine, so the borrowed span stays valid.
        const auto load = [](auto& dst, const std::vector<std::uint8_t>& src) {
            std::copy_n(src.begin(), std::min(src.size(), dst.size()), dst.begin());
        };
        load(tx->m_bios, bios.m_bios);
        load(tx->s_bios, bios.s_bios);
        load(tx->g_bios, bios.g_bios);
        tx->attach_cart(g->rom);

        // Power-on adapter state: ADEN clear, SH-2s parked. The 68000 boots the
        // cartridge's own reset vectors in plain-Genesis mode and runs the
        // standard 32X security block: probe the adapter ID, enable ADEN (the
        // map switch), then release the SH-2 /RES line -- all through $A15100.
        // From there the real boot ROM protocol carries the handshake: the
        // master posts the cart checksum + M_OK, the slave (cart present) takes
        // its entry from the cart's SH-2 table and posts S_OK.

        // 68000-side 32X cart remap. $880000-$8FFFFF views the first 512 KiB of
        // the cart with no vector overlay (the boot code reads the cart's own
        // security block and vectors here); $900000-$9FFFFF is the 1 MiB banked
        // window at its power-on bank 0 (the bank-select write path is deferred
        // with the DREQ group). Reads past the cart fall to open bus.
        const std::span<const std::uint8_t> rom{g->rom};
        bus.map_rom(0x880000U, rom.first(std::min<std::size_t>(rom.size(), 0x80000U)), 1);
        bus.map_rom(0x900000U, rom.first(std::min<std::size_t>(rom.size(), 0x100000U)), 1);
        // The G BIOS vectors overlay $000000 only once the security block sets
        // ADEN (the $A15100 write path maps it); until then the 68000 runs on
        // the cart's own vectors, exactly like a plain Genesis.
        machine->g_bios_present = !bios.g_bios.empty();

        // The adapter identity the security block probes before enabling:
        // ASCII "MARS" at $A130EC, readable regardless of ADEN.
        static constexpr std::uint8_t mars_id[4]{0x4DU, 0x41U, 0x52U, 0x53U};
        bus.map_rom(0xA130ECU, {mars_id, 4U}, 1);

        // 32X interrupt sources from the Genesis VDP. VINT rides the
        // unconditional V-blank edge -- a 32X game may never enable the 68000's
        // V-int, but the pulse still reaches the adapter and both SH-2 latches.
        // The wrapper preserves the stock Genesis vblank behaviour (Z80 INT,
        // frame counter, pad timeouts) and mirrors V-blank into adapter-control
        // bit 7, which games poll as a frame-sync barrier.
        g->vdp.set_vblank_callback([g, tx](bool in_vblank) {
            g->on_vblank(in_vblank);
            // Drive the 32X VDP's VBLK status bit and its frame-select
            // flip-flop from the same edge (HBLK joins with the per-scanline
            // player loop in phase D), then swing the $04000000 window onto
            // the new access bank -- the double-buffer swap.
            tx->vdp.set_blanking(false, in_vblank);
            tx->set_fb_access_bank(tx->vdp.access_bank());
            if (in_vblank) {
                tx->adapter_ctrl |= 0x0080U;
                tx->raise_vint();
            } else {
                tx->adapter_ctrl &= static_cast<std::uint16_t>(~0x0080U);
            }
        });
        // HINT taps the VDP's /HINT latch edge (line counter expired with IE1
        // set); the 68000 keeps its own pending-level path untouched.
        g->vdp.set_hint_callback([tx] { tx->raise_hint(); });

        // $A15100-$A1513F: the 32X system registers as seen by the 68000 --
        // adapter control, INTM/INTS, the COMM bank, and the PWM scaffold. Byte
        // handlers promote to word read-modify-write (big-endian lanes: the even
        // address is the high byte). Priority 1 keeps the window above the
        // cartridge ROM; it exists only on a 32X machine, a plain Genesis never
        // maps it.
        bus.map_mmio(
            0xA15100U, 0x40U,
            [tx](std::uint32_t a) -> std::uint8_t {
                const std::uint32_t off = (a - 0xA15100U) & ~1U;
                const std::uint16_t w = m68k_reg_read_word(*tx, off);
                return (a & 1U) != 0U ? static_cast<std::uint8_t>(w)
                                      : static_cast<std::uint8_t>(w >> 8U);
            },
            [tx, machine = machine.get()](std::uint32_t a, std::uint8_t value) {
                const std::uint32_t off = (a - 0xA15100U) & ~1U;
                if (off >= 0x20U && off <= 0x2FU) {
                    // COMM write: let the SH-2s observe the pre-write state
                    // first. The boot handshake interlocks on consecutive 68000
                    // COMM writes (clear-then-command) landing in the same
                    // interleave slice -- a hardware SH-2 polls continuously
                    // and never misses the window.
                    machine->catch_up_sh2();
                }
                if (off == 0x12U) {
                    // The DREQ FIFO write port pushes once per completed word:
                    // the even byte latches the high half, the odd completes.
                    // A full queue means the SH-2 DMA hasn't run yet this
                    // slice -- catch the SH-2 pair up mid-slice so the drain
                    // happens "now", as it would on hardware, instead of
                    // dropping stream words (dropped PCM is white noise).
                    if ((a & 1U) == 0U) {
                        tx->dreq_write_high = value;
                    } else if ((tx->dreq_ctrl & 0x0004U) != 0U) {
                        if (tx->dreq_fifo_count >= sega32x_system::dreq_fifo_depth) {
                            machine->catch_up_sh2();
                        }
                        tx->dreq_fifo_push(static_cast<std::uint16_t>(
                            (static_cast<std::uint16_t>(tx->dreq_write_high) << 8U) | value));
                    }
                    return;
                }
                const std::uint16_t cur = m68k_reg_read_word(*tx, off);
                const std::uint16_t next =
                    (a & 1U) != 0U
                        ? static_cast<std::uint16_t>((cur & 0xFF00U) | value)
                        : static_cast<std::uint16_t>((cur & 0x00FFU) |
                                                     (static_cast<std::uint16_t>(value) << 8U));
                m68k_reg_write_word(*machine, off, next);
            },
            1);

        // $A15180-$A1518F: the 32X VDP register cells as seen by the 68000 --
        // the same chip the SH-2s reach at +$4100, including the autofill
        // trigger on a completed DATA write.
        bus.map_mmio(
            0xA15180U, 0x10U, [tx](std::uint32_t a) { return tx->vdp_reg_read(a - 0xA15180U); },
            [tx](std::uint32_t a, std::uint8_t value) { tx->vdp_reg_write(a - 0xA15180U, value); },
            1);

        // $A15200-$A153FF: the 32X palette CRAM as seen by the 68000 (the SH-2
        // +$4200 window). Boot code write-verifies it word by word.
        bus.map_mmio(
            0xA15200U, 0x200U, [tx](std::uint32_t a) { return tx->vdp_pal_read(a - 0xA15200U); },
            [tx](std::uint32_t a, std::uint8_t value) { tx->vdp_pal_write(a - 0xA15200U, value); },
            1);

        return machine;
    }

} // namespace mnemos::manifests::sega32x
