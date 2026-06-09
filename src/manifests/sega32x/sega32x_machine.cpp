#include "sega32x_machine.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

namespace mnemos::manifests::sega32x {

    namespace {

        // 68000-side 32X register window, word-level ($A15100 + off). Byte access
        // is promoted to read-modify-write on the 16-bit cell by the bus handlers
        // below, so handshake semantics (reset-bit toggles, COMM byte writes)
        // behave the way retail code expects.
        std::uint16_t m68k_reg_read_word(const sega32x_system& tx, std::uint32_t off) {
            // Adapter control -- $A15100. Bit 15=FM, bit 7=RV, bit 1=ADEN, bit
            // 0=RES (0 = SH-2s held in reset). RES reflects the live /RES line,
            // not a latch.
            if (off == 0x00U) {
                std::uint16_t v = tx.adapter_ctrl;
                if (tx.sh2_reset_asserted) {
                    v &= static_cast<std::uint16_t>(~0x0001U);
                } else {
                    v |= 0x0001U;
                }
                return v;
            }
            // Interrupt control: $A15102 = INTM (master), $A15104 = INTS (slave).
            // Only the CMD bit is 68000-visible -- the VINT/HINT/PWM enables are
            // SH-2-private (each SH-2's own program sets them through its
            // system-register window).
            if (off == 0x02U) {
                return tx.master_irq_mask & sega32x_system::irq_cmd;
            }
            if (off == 0x04U) {
                return tx.slave_irq_mask & sega32x_system::irq_cmd;
            }
            if (off == 0x06U) {
                return 0U; // DREQ control: not yet modelled
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

        void m68k_reg_write_word(sega32x_system& tx, std::uint32_t off, std::uint16_t val) {
            if (off == 0x00U) {
                // The register directly drives the SH-2 /RES pin: ADEN+RES=1
                // releases (a re-release while running is safe -- the BIOS checks
                // ADEN at its reset vector), RES=0 always parks. The non-reset
                // bits stay latched so game code reads back its own writes.
                const bool aden = (val & 0x0002U) != 0U;
                const bool res_release = (val & 0x0001U) != 0U;
                tx.adapter_ctrl = val & 0xFFFEU;
                if (aden && res_release) {
                    tx.set_sh2_reset(false);
                } else if (!res_release) {
                    tx.set_sh2_reset(true);
                }
                return;
            }
            // INTM/INTS: the 68000 may only drive the CMD-enable bit; a 0->1
            // transition asserts the CMD interrupt on the targeted SH-2. The
            // other enable bits belong to that SH-2's own program -- letting a
            // 68000 bank-select write at $A15104 enable the slave's VINT would
            // deadlock the boot handshake.
            if (off == 0x02U) {
                const std::uint8_t prev = tx.master_irq_mask;
                const auto next = static_cast<std::uint8_t>((prev & ~sega32x_system::irq_cmd) |
                                                            (val & sega32x_system::irq_cmd));
                tx.set_master_irq_mask(next);
                if ((next & ~prev & sega32x_system::irq_cmd) != 0U) {
                    tx.raise_cmd_master();
                }
                return;
            }
            if (off == 0x04U) {
                const std::uint8_t prev = tx.slave_irq_mask;
                const auto next = static_cast<std::uint8_t>((prev & ~sega32x_system::irq_cmd) |
                                                            (val & sega32x_system::irq_cmd));
                tx.set_slave_irq_mask(next);
                if ((next & ~prev & sega32x_system::irq_cmd) != 0U) {
                    tx.raise_cmd_slave();
                }
                return;
            }
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

        // 68000-side 32X cart remap. $880000-$8FFFFF views the first 512 KiB of
        // the cart with no vector overlay (the boot code reads the cart's own
        // security block and vectors here); $900000-$9FFFFF is the 1 MiB banked
        // window at its power-on bank 0 (the bank-select write path is deferred
        // with the DREQ group). Reads past the cart fall to open bus.
        const std::span<const std::uint8_t> rom{g->rom};
        bus.map_rom(0x880000U, rom.first(std::min<std::size_t>(rom.size(), 0x80000U)), 1);
        bus.map_rom(0x900000U, rom.first(std::min<std::size_t>(rom.size(), 0x100000U)), 1);
        // With a G BIOS present, its vectors overlay the bottom of cart space so
        // the console boots the adapter's security/handshake code first. The
        // 68000 fetched its reset vectors from the cart during assemble_genesis,
        // before the overlay existed -- re-reset it so boot starts in the BIOS.
        if (!bios.g_bios.empty()) {
            bus.map_rom(0x000000U, tx->g_bios, 1);
            g->cpu.reset(chips::reset_kind::power_on);
        }

        // 32X interrupt sources from the Genesis VDP. VINT rides the
        // unconditional V-blank edge -- a 32X game may never enable the 68000's
        // V-int, but the pulse still reaches the adapter and both SH-2 latches.
        // The wrapper preserves the stock Genesis vblank behaviour (Z80 INT,
        // frame counter, pad timeouts) and mirrors V-blank into adapter-control
        // bit 7, which games poll as a frame-sync barrier.
        g->vdp.set_vblank_callback([g, tx](bool in_vblank) {
            g->on_vblank(in_vblank);
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
            [tx](std::uint32_t a, std::uint8_t value) {
                const std::uint32_t off = (a - 0xA15100U) & ~1U;
                const std::uint16_t cur = m68k_reg_read_word(*tx, off);
                const std::uint16_t next =
                    (a & 1U) != 0U
                        ? static_cast<std::uint16_t>((cur & 0xFF00U) | value)
                        : static_cast<std::uint16_t>((cur & 0x00FFU) |
                                                     (static_cast<std::uint16_t>(value) << 8U));
                m68k_reg_write_word(*tx, off, next);
            },
            1);

        return machine;
    }

} // namespace mnemos::manifests::sega32x
