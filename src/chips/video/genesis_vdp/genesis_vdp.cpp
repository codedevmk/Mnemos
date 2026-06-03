#include "genesis_vdp.hpp"

#include "chip_registry.hpp"
#include "genesis_vdp_hcounter_tables.hpp"
#include "state.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>

namespace mnemos::chips::video {
    namespace {
        // 68K->VDP DMA wraps the source within a 128KB bank (the top bits are fixed).
        [[nodiscard]] constexpr std::uint32_t dma_src_advance(std::uint32_t source,
                                                              std::uint32_t bytes) noexcept {
            return (source & ~0x1FFFFU) | ((source + bytes) & 0x1FFFFU);
        }

        // Command codes whose low nibble selects a memory-read target.
        [[nodiscard]] constexpr bool is_read_target(int code_low) noexcept {
            return code_low == 0 || code_low == 4 || code_low == 8 || code_low == 0x0C;
        }

        // VDP FIFO access-slot timing tables (within-line master-cycle offsets at
        // which a FIFO slot drains), from the hardware's documented per-line slot
        // schedule. Each table is the line's access slots followed by the FIRST 12
        // of those slots repeated +one line, so a FIFO entry positioned late in a
        // line can still find a drain slot in the following line. (It is not a full
        // shifted copy -- the tail slots of the line are omitted in the +line
        // half.) Used by the data-port write back-pressure model (see data_write).
        constexpr int kLineMaster = genesis_vdp::master_clocks_per_line;
        constexpr std::array<int, 28> fifo_timing_h32 = {
            230,
            510,
            810,
            970,
            1130,
            1450,
            1610,
            1770,
            2090,
            2250,
            2410,
            2730,
            2890,
            3050,
            3350,
            3370,
            kLineMaster + 230,
            kLineMaster + 510,
            kLineMaster + 810,
            kLineMaster + 970,
            kLineMaster + 1130,
            kLineMaster + 1450,
            kLineMaster + 1610,
            kLineMaster + 1770,
            kLineMaster + 2090,
            kLineMaster + 2250,
            kLineMaster + 2410,
            kLineMaster + 2730,
        };
        constexpr std::array<int, 30> fifo_timing_h40 = {
            352,
            820,
            948,
            1076,
            1332,
            1460,
            1588,
            1844,
            1972,
            2100,
            2356,
            2484,
            2612,
            2868,
            2996,
            3124,
            3364,
            3380,
            kLineMaster + 352,
            kLineMaster + 820,
            kLineMaster + 948,
            kLineMaster + 1076,
            kLineMaster + 1332,
            kLineMaster + 1460,
            kLineMaster + 1588,
            kLineMaster + 1844,
            kLineMaster + 1972,
            kLineMaster + 2100,
            kLineMaster + 2356,
            kLineMaster + 2484,
        };

        // Next external-access slot at/after `pos` (master cycles from the current
        // line start; may span lines for a long burst -- slots repeat every
        // kLineMaster). Used by the opt-in write-accept gating model.
        [[nodiscard]] std::int64_t next_accept_slot(bool h40, std::int64_t pos) noexcept {
            const std::span<const int> t =
                h40 ? std::span<const int>(fifo_timing_h40) : std::span<const int>(fifo_timing_h32);
            const std::int64_t line = pos / kLineMaster;
            const std::int64_t within = pos - line * kLineMaster;
            for (const int s : t) {
                if (s >= kLineMaster) {
                    break; // past the within-line slots (the +line repeat half)
                }
                if (s >= within) {
                    return line * kLineMaster + s;
                }
            }
            return (line + 1) * kLineMaster + t[0]; // roll into the next line's first slot
        }

        // Opt-in (MNEMOS_WRITE_ACCEPT): model VDP data-port write ACCEPT timing -- each
        // 16-bit word waits for the next access slot during active display -- not just
        // FIFO-full back-pressure. Off by default (one cached check), no behaviour change.
        [[nodiscard]] bool write_accept_enabled() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv: opt-in diagnostic, not hot-path
#endif
            static const bool on = std::getenv("MNEMOS_WRITE_ACCEPT") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return on;
        }

        // Tuning probe (MNEMOS_ACCEPT_MIN_GAP, default 0): minimum master cycles
        // between consecutive accepted data-port words. The 68K cannot consume every
        // documented drain slot, so the sustained accept cadence is sparser than the
        // 18-slot drain schedule; this paces a burst to >= min_gap master/word.
        [[nodiscard]] std::int64_t write_accept_min_gap() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            static const std::int64_t gap = [] {
                const char* e = std::getenv("MNEMOS_ACCEPT_MIN_GAP");
                return e != nullptr ? std::strtoll(e, nullptr, 10) : 0LL;
            }();
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            return gap;
        }

        // Scroll-plane size in cells (R16 HSZ/VSZ; 0b10 is invalid -> treat as 64).
        [[nodiscard]] constexpr int scroll_size(int sz_bits) noexcept {
            constexpr std::array<int, 4> sizes = {32, 64, 64, 128};
            return sizes[static_cast<std::size_t>(sz_bits) & 3U];
        }

        // CRAM channel intensity ramp: 3-bit CRAM -> 4-bit per shade mode.
        //   shadow    -> v       (0..7)
        //   normal    -> v << 1  (0,2,4,..,14)
        //   highlight -> v + 7   (7..14)
        [[nodiscard]] constexpr int channel_intensity(int v3, std::uint8_t shade_mode) noexcept {
            if (shade_mode == 0) {
                return v3;
            }
            if (shade_mode == 2) {
                return v3 + 7;
            }
            return v3 << 1;
        }
        // 4-bit channel -> 8-bit through a 5:6:5 round-trip. R and B use 5-bit
        // precision, G uses 6-bit; this asymmetry is what produces the
        // observed max-white of 0xEF rather than 0xFF.
        [[nodiscard]] constexpr std::uint8_t pack_rb_8(int v4) noexcept {
            const int five = ((v4 & 0xF) << 1) | ((v4 >> 3) & 1);
            return static_cast<std::uint8_t>((five << 3) | (five >> 2));
        }
        [[nodiscard]] constexpr std::uint8_t pack_g_8(int v4) noexcept {
            const int six = ((v4 & 0xF) << 2) | ((v4 >> 2) & 3);
            return static_cast<std::uint8_t>((six << 2) | (six >> 4));
        }
    } // namespace

    chip_metadata genesis_vdp::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "315-5313",
            .family = "VDP",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    // ------------------------------------------------------------------------
    //  Geometry + H/V readback
    // ------------------------------------------------------------------------

    int genesis_vdp::visible_width() const noexcept { return h40_mode() ? 320 : 256; }

    int genesis_vdp::field_height() const noexcept {
        // V30 enables 240 visible scanlines per field. On real hardware V30 is
        // only fully supported on PAL (NTSC garbles the bottom 16 lines on
        // a CRT), but many games enable V30 on NTSC for non-gameplay screens
        // (credits, intros) and the VDP still renders all 240 lines into the
        // framebuffer. We honour that so the emulated frame matches what the
        // game wrote -- the bottom 16 rows are no worse than on hardware.
        return v30_mode() ? 240 : 224;
    }

    int genesis_vdp::visible_height() const noexcept {
        return interlace_enabled() ? field_height() * 2 : field_height();
    }

    int genesis_vdp::total_hclocks() const noexcept { return h40_mode() ? 420 : 342; }

    std::uint8_t genesis_vdp::vcounter_readback(int scanline, bool odd_frame) const noexcept {
        if (interlace_enabled()) {
            if (scanline < 0) {
                scanline = 0;
            } else if (scanline >= total_scanlines_) {
                scanline %= total_scanlines_;
            }
            const auto ivc = static_cast<std::uint16_t>(scanline * 2 + (odd_frame ? 1 : 0));
            return static_cast<std::uint8_t>((ivc & 0xFEU) | ((ivc >> 8U) & 0x01U));
        }
        if (scanline <= 0xEA) {
            return static_cast<std::uint8_t>(scanline);
        }
        return static_cast<std::uint8_t>(scanline - (total_scanlines_ - 0x100));
    }

    std::uint8_t genesis_vdp::hcounter_readback(int sample) const noexcept {
        const int total = total_hclocks();
        if (sample < 0) {
            sample = 0;
        } else if (sample >= total) {
            sample = total - 1;
        }
        int master = (sample * master_clocks_per_line) / total;
        if (master < 0) {
            master = 0;
        } else if (master >= master_clocks_per_line) {
            master = master_clocks_per_line - 1;
        }
        const auto m = static_cast<std::size_t>(master);
        return h40_mode() ? cycle2hc40[m] : cycle2hc32[m];
    }

    bool genesis_vdp::hcounter_in_hblank(int sample) const noexcept {
        const std::uint8_t hc = hcounter_readback(sample);
        return h40_mode() ? (hc >= 0xB3U || hc <= 0x05U) : (hc >= 0x93U || hc <= 0x04U);
    }

    std::uint16_t genesis_vdp::hv_counter_live() const noexcept {
        const std::uint8_t hc = hcounter_readback(hcounter_);
        const std::uint8_t vc = vcounter_readback(scanline_, odd_frame_);
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(vc) << 8U) | hc);
    }

    std::uint16_t genesis_vdp::dma_length() const noexcept {
        return static_cast<std::uint16_t>(reg_[19] | (static_cast<std::uint16_t>(reg_[20]) << 8U));
    }

    std::uint32_t genesis_vdp::dma_source() const noexcept {
        return static_cast<std::uint32_t>(reg_[21]) | (static_cast<std::uint32_t>(reg_[22]) << 8U) |
               (static_cast<std::uint32_t>(reg_[23] & 0x7FU) << 16U);
    }

    // ------------------------------------------------------------------------
    //  Memory access primitives
    // ------------------------------------------------------------------------

    std::uint16_t genesis_vdp::vram_read16(std::uint32_t addr) const noexcept {
        addr &= 0xFFFEU;
        return static_cast<std::uint16_t>((vram_[addr] << 8U) | vram_[addr + 1U]);
    }

    void genesis_vdp::vram_write16(std::uint32_t addr, std::uint16_t value) noexcept {
        addr &= 0xFFFFU;
        if ((addr & 1U) != 0U) {
            const std::uint32_t even = (addr - 1U) & 0xFFFFU;
            vram_[even] = static_cast<std::uint8_t>(value);
            vram_[(even + 1U) & 0xFFFFU] = static_cast<std::uint8_t>(value >> 8U);
            return;
        }
        vram_[addr] = static_cast<std::uint8_t>(value >> 8U);
        vram_[(addr + 1U) & 0xFFFFU] = static_cast<std::uint8_t>(value);
    }

    void genesis_vdp::vram_write8(std::uint32_t addr, std::uint8_t value) noexcept {
        // Byte writes swap the byte within the word (Genesis quirk).
        vram_[(addr & 0xFFFFU) ^ 1U] = value;
    }

    void genesis_vdp::cram_write(int idx, std::uint16_t value) noexcept {
        cram_[static_cast<std::size_t>(idx) & 0x3FU] = static_cast<std::uint16_t>(value & 0x0EEEU);
    }

    void genesis_vdp::vsram_write(int idx, std::uint16_t value) noexcept {
        if (idx >= 0 && idx < vsram_entries) {
            vsram_[static_cast<std::size_t>(idx)] = static_cast<std::uint16_t>(value & 0x07FFU);
        }
    }

    void genesis_vdp::write_target_word(int code_low, std::uint16_t word) noexcept {
        if (code_low == 1) {
            vram_write16(cmd_addr_, word);
        } else if (code_low == 3) {
            cram_write(static_cast<int>((cmd_addr_ >> 1U) & 0x3FU), word);
        } else if (code_low == 5) {
            vsram_write(static_cast<int>((cmd_addr_ >> 1U) & 0x3FU), word);
        }
    }

    std::uint16_t genesis_vdp::data_prefetch() noexcept {
        const int code_low = cmd_code_ & 0x0F;
        if (code_low == 0) {
            return vram_read16(cmd_addr_);
        }
        if (code_low == 8) { // CRAM read: colour bits from CRAM, rest open-bus
            const std::uint16_t c = cram_[(cmd_addr_ >> 1U) & 0x3FU];
            return static_cast<std::uint16_t>((c & 0x0EEEU) | (read_buffer_ & 0xF111U));
        }
        if (code_low == 4) { // VSRAM read
            const std::size_t idx = (cmd_addr_ >> 1U) & 0x3FU;
            const std::uint16_t vs = idx < vsram_entries ? vsram_[idx] : std::uint16_t{0};
            return static_cast<std::uint16_t>((vs & 0x07FFU) | (read_buffer_ & 0xF800U));
        }
        if (code_low == 0x0C) { // VRAM byte read
            return static_cast<std::uint16_t>((read_buffer_ & 0xFF00U) |
                                              vram_[(cmd_addr_ & 0xFFFFU) ^ 1U]);
        }
        return read_buffer_;
    }

    // ------------------------------------------------------------------------
    //  Command + data ports
    // ------------------------------------------------------------------------

    void genesis_vdp::ctrl_write(std::uint16_t value) noexcept {
        // Register write: 10xx xxxx xxxx xxxx (only when no command is pending).
        if ((value & 0xC000U) == 0x8000U && !cmd_pending_) {
            const int reg = (value >> 8U) & 0x1F;
            if (reg < register_count) {
                // Mode-4 lockout: with M5 (reg1 bit2) clear, registers above $0A are
                // masked (SMS-compat mode).
                const bool masked = ((reg_[1] & 0x04U) == 0U) && reg > 0x0A;
                const std::uint8_t old = reg_[static_cast<std::size_t>(reg)];
                if (!masked) {
                    reg_[static_cast<std::size_t>(reg)] = static_cast<std::uint8_t>(value & 0xFFU);
                }
                if (reg == 0 && !masked) {
                    const bool old_latch = (old & 0x02U) != 0U;
                    const bool new_latch = hv_latch_enabled();
                    if (!old_latch && new_latch) {
                        hv_latched_ = true;
                        hv_latch_value_ = hv_counter_live();
                    } else if (old_latch && !new_latch) {
                        hv_latched_ = false;
                    }
                }
                if (reg == 1 && !masked) {
                    // Enabling V-int while a VINT is already latched raises
                    // the CPU IRQ with a one-instruction latency: the CPU
                    // completes the enabling MOVE and the next instruction
                    // before accepting the IRQ, so the saved PC points past
                    // the post-MOVE instruction.
                    const bool new_vint_en = (reg_[1] & 0x20U) != 0U;
                    const bool old_vint_en = (old & 0x20U) != 0U;
                    if (!old_vint_en && new_vint_en && vint_happened_) {
                        if (delayed_irq_callback_) {
                            // The CPU's delayed-IRQ path raises irq_level_=6
                            // directly when the latency window closes; do not
                            // set vblank_pending_ here, otherwise the next
                            // refresh_irq() would race and raise the IRQ
                            // immediately.
                            delayed_irq_callback_(6);
                        } else {
                            // No delayed-IRQ wiring (unit-test path). Raise
                            // immediately via the regular pending route.
                            vblank_pending_ = true;
                            refresh_irq();
                        }
                    }
                }
            }
            cmd_code_ = static_cast<std::uint8_t>((cmd_code_ & 0x3CU) | ((value >> 14U) & 0x03U));
            return;
        }

        if (!cmd_pending_) {
            // First word of a two-word command.
            cmd_first_ = value;
            cmd_pending_ = true;
            cmd_addr_ = (cmd_addr_ & 0xC000U) | (value & 0x3FFFU);
            cmd_code_ = static_cast<std::uint8_t>((cmd_code_ & 0x3CU) | ((value >> 14U) & 0x03U));
            return;
        }

        // Second word completes the command.
        cmd_pending_ = false;
        cmd_addr_ = (cmd_first_ & 0x3FFFU) | (static_cast<std::uint32_t>(value & 0x03U) << 14U);
        cmd_code_ =
            static_cast<std::uint8_t>(((cmd_first_ >> 14U) & 0x03U) | ((value >> 2U) & 0x3CU));

        if ((cmd_code_ & 0x20U) != 0U && dma_enabled()) {
            const int type = dma_type();
            if (type == 2) {
                // VRAM fill: wait for the data-port write that supplies the value.
                // DMA-busy is NOT asserted yet -- the fill is merely pending; real
                // hardware only asserts busy once the data-port write actually starts
                // the fill. Some titles defensively poll dma_busy after the
                // command and would deadlock if we claimed busy here.
                dma_fill_pending_ = true;
            } else if (type == 3) {
                // VRAM copy (runs immediately in this functional model).
                std::uint16_t len = dma_length();
                if (len == 0U) {
                    len = 0xFFFFU;
                }
                dma_source_ = static_cast<std::uint32_t>(reg_[21]) |
                              (static_cast<std::uint32_t>(reg_[22]) << 8U);
                for (std::uint16_t i = 0; i < len; ++i) {
                    dma_copy_step();
                }
                // VRAM-to-VRAM copy (dma_type 3) runs on the VDP's internal data
                // path: it does NOT stall the 68K bus. Hold the DMA-busy status
                // bit for the copy duration so a game that defensively polls
                // dma_busy after the command spins until the copy completes (the
                // timer is drained in tick(), which flips dma_busy_ back to 0).
                dma_busy_ = true;
                dma_busy_master_cycles_ += estimate_dma_transfer_cycles(len, 3);
                reg_[19] = 0U;
                reg_[20] = 0U;
                reg_[21] = static_cast<std::uint8_t>(dma_source_);
                reg_[22] = static_cast<std::uint8_t>(dma_source_ >> 8U);
            } else if (dma_read_) {
                // 68K -> VRAM/CRAM/VSRAM transfer (runs immediately).
                std::uint16_t len = dma_length();
                if (len == 0U) {
                    len = 0xFFFFU;
                }
                std::uint32_t src = dma_source() << 1U; // word address -> byte
                dma_busy_ = true;
                for (std::uint16_t i = 0; i < len; ++i) {
                    const std::uint16_t word = dma_read_(src);
                    src = dma_src_advance(src, 2U);
                    dma_transfer_step(word);
                }
                dma_busy_ = false;
                // 68K -> VDP transfer: target VRAM (code 1) = dma_type 1,
                // target CRAM/VSRAM (code 3/5) = dma_type 0. Both stall the
                // 68K bus -- modelled by dma_stall_master_cycles_.
                const int code_low = cmd_code_ & 0x0F;
                const int dma_type = (code_low & 0x06) ? 0 : 1;
                dma_stall_master_cycles_ += estimate_dma_transfer_cycles(len, dma_type);
                reg_[19] = 0U;
                reg_[20] = 0U;
                const std::uint32_t new_src = src >> 1U;
                reg_[21] = static_cast<std::uint8_t>(new_src);
                reg_[22] = static_cast<std::uint8_t>(new_src >> 8U);
                reg_[23] =
                    static_cast<std::uint8_t>((reg_[23] & 0x80U) | ((new_src >> 16U) & 0x7FU));
            }
        }

        if (is_read_target(cmd_code_ & 0x0F)) {
            read_buffer_ = data_prefetch();
        }
    }

    void genesis_vdp::fifo_data_write() noexcept {
        // Back-pressure applies only during active display: the hardware gates it
        // on the display being enabled (reg[1] bit 6) and not being in V-blank.
        // Outside active display the FIFO drains freely.
        if (in_vblank_ || !display_enabled()) {
            return;
        }

        if (write_accept_enabled()) {
            // Each data-port word waits for the next VDP external access slot, so a
            // back-to-back write burst paces to ~1 word/slot (matching hardware
            // /DTACK accept timing) instead of running at raw opcode cadence.
            // effective_now folds in the pending stall so a MOVE.L's two words charge
            // two SEQUENTIAL slots, not the same one twice.
            const std::int64_t effective_now = current_line_master() + fifo_stall_master_cycles_;
            std::int64_t base = effective_now > fifo_accept_cursor_master_
                                    ? effective_now
                                    : fifo_accept_cursor_master_;
            // Optional sparser accept cadence: keep each accept at least min_gap past
            // the previous one (the 68K can't consume every drain slot).
            const std::int64_t min_gap = write_accept_min_gap();
            if (min_gap > 0) {
                const std::int64_t min_base = fifo_accept_cursor_master_ - 1 + min_gap;
                if (base < min_base) {
                    base = min_base;
                }
            }
            const std::int64_t accept = next_accept_slot(h40_mode(), base);
            fifo_stall_master_cycles_ += accept - effective_now;
            fifo_accept_cursor_master_ = accept + 1;
            // The accept-cadence model is independent of the 4-entry FIFO-drain
            // schedule: leave fifo_drain_/fifo_idx_ untouched so they keep meaning
            // exactly "drain times" (as documented + serialized).
            return;
        }

        const auto& timing = h40_mode() ? std::span<const int>(fifo_timing_h40)
                                        : std::span<const int>(fifo_timing_h32);
        const std::int64_t now = current_line_master();

        // Position the new entry against the existing FIFO.
        std::int64_t cycles = now;
        const std::int64_t newest = fifo_drain_[static_cast<std::size_t>((fifo_idx_ + 3) & 3)];
        if (now < newest) {
            // FIFO not empty. If the oldest entry has not drained yet the FIFO is
            // full and the 68K must wait for it; accrue that wait as stall debt.
            const std::int64_t oldest = fifo_drain_[static_cast<std::size_t>(fifo_idx_)];
            if (now < oldest) {
                fifo_stall_master_cycles_ += oldest - now;
            }
            // The new entry is processed after the current newest entry.
            cycles = newest;
        }

        // Find the next free access slot at/after `cycles` and record this entry's
        // drain time there. Word writes consume one slot (byte writes two, but the
        // 68K data port is word-wide here, so byte_access = 0).
        std::size_t slot = 0;
        while (slot < timing.size() && cycles >= timing[slot]) {
            ++slot;
        }
        if (slot >= timing.size()) {
            slot = timing.size() - 1;
        }
        fifo_drain_[static_cast<std::size_t>(fifo_idx_)] = timing[slot];
        fifo_idx_ = (fifo_idx_ + 1) & 3;
    }

    void genesis_vdp::dma_transfer_step(std::uint16_t word) noexcept {
        write_target_word(cmd_code_ & 0x0F, word);
        cmd_addr_ = (cmd_addr_ + autoincrement()) & 0xFFFFU;
    }

    void genesis_vdp::dma_copy_step() noexcept {
        const std::uint16_t src = static_cast<std::uint16_t>(dma_source_ & 0xFFFFU);
        const std::uint16_t dst = static_cast<std::uint16_t>(cmd_addr_ & 0xFFFFU);
        vram_[dst ^ 1U] = vram_[src ^ 1U];
        dma_source_ = (dma_source_ + 1U) & 0xFFFFU;
        cmd_addr_ = (cmd_addr_ + autoincrement()) & 0xFFFFU;
    }

    void genesis_vdp::dma_fill_step() noexcept {
        if (dma_fill_code_ == 1) {
            vram_write8(cmd_addr_, dma_fill_byte_);
        } else if (dma_fill_code_ == 3) {
            cram_write(static_cast<int>((cmd_addr_ >> 1U) & 0x3FU), dma_fill_word_);
        } else if (dma_fill_code_ == 5) {
            vsram_write(static_cast<int>((cmd_addr_ >> 1U) & 0x3FU), dma_fill_word_);
        }
        cmd_addr_ = (cmd_addr_ + autoincrement()) & 0xFFFFU;
    }

    void genesis_vdp::data_write(std::uint16_t value) noexcept {
        const int code_low = cmd_code_ & 0x0F;
        cmd_pending_ = false;

        if (dma_fill_pending_) {
            dma_fill_pending_ = false;
            std::uint32_t len = dma_length();
            if (len == 0U) {
                len = 0x10000U;
            }
            dma_fill_byte_ = static_cast<std::uint8_t>(value >> 8U);
            dma_fill_code_ = static_cast<std::uint8_t>(code_low);
            dma_fill_word_ = value;
            // The trigger write lands as a normal write first.
            write_target_word(code_low, value);
            cmd_addr_ = (cmd_addr_ + autoincrement()) & 0xFFFFU;
            for (std::uint32_t i = 0; i < len; ++i) {
                dma_fill_step();
            }
            const std::uint32_t fill_src = dma_src_advance(dma_source() << 1U, len * 2U) >> 1U;
            // VRAM fill (dma_type 2) runs on the VDP's internal data path: it
            // does NOT stall the 68K bus. Hold the DMA-busy status bit for the
            // fill duration so a game that defensively polls dma_busy spins until
            // the fill completes (the timer is drained in tick()). The initial
            // data-port write that primes the fill costs 2 extra access slots
            // before the transfer proper begins, so the busy duration is modelled
            // as estimate_dma_transfer_cycles(len + 2, 2).
            dma_busy_ = true;
            dma_busy_master_cycles_ += estimate_dma_transfer_cycles(len + 2U, 2);
            reg_[19] = 0U;
            reg_[20] = 0U;
            reg_[21] = static_cast<std::uint8_t>(fill_src);
            reg_[22] = static_cast<std::uint8_t>(fill_src >> 8U);
            reg_[23] = static_cast<std::uint8_t>((reg_[23] & 0x80U) | ((fill_src >> 16U) & 0x7FU));
            read_buffer_ = value;
            return;
        }

        // A plain 68K data-port write passes through the VDP write FIFO; during
        // active display it back-pressures the 68K when the FIFO fills.
        fifo_data_write();

        write_target_word(code_low, value);
        read_buffer_ = value;
        cmd_addr_ = (cmd_addr_ + autoincrement()) & 0xFFFFU;
    }

    std::uint16_t genesis_vdp::data_read() noexcept {
        const int code_low = cmd_code_ & 0x0F;
        cmd_pending_ = false;
        const std::uint16_t value = read_buffer_;
        cmd_addr_ = (cmd_addr_ + autoincrement()) & 0xFFFFU;
        if (is_read_target(code_low)) {
            read_buffer_ = data_prefetch();
        }
        return value;
    }

    std::uint16_t genesis_vdp::status_read() noexcept {
        std::uint16_t s = 0;
        // HBLANK status bit is high during the latter portion of each line.
        // Approximate the active/hblank split at ~74% of the line cycle
        // (matches H40's ~2540-master active region of a 3420-master line);
        // good enough for status-poll loops that synchronise on HBLANK.
        const bool in_hblank_phase =
            in_hblank_ || line_accumulator_ >= (master_clocks_per_line * 3) / 4;

        // FIFO-empty (bit 9) is reported as always-empty / never-full (bit 8 = 0):
        // the write FIFO is modelled for 68K back-pressure timing (see
        // fifo_data_write) but its live occupancy is not surfaced to status reads
        // yet. Deriving the real empty/full bits from the drain schedule is a
        // follow-up (some titles pace writes off these bits; needs its own sweep).
        s |= (1U << 9U);
        if (vint_happened_) {
            s |= (1U << 7U);
        }
        if (sprite_overflow_) {
            s |= (1U << 6U);
        }
        if (sprite_collision_) {
            s |= (1U << 5U);
        }
        if (odd_frame_) {
            s |= (1U << 4U);
        }
        if (in_vblank_) {
            s |= (1U << 3U);
        }
        if (in_hblank_phase) {
            s |= (1U << 2U);
        }
        if (dma_busy_) {
            s |= (1U << 1U);
        }
        if (pal_mode_) {
            s |= (1U << 0U);
        }

        // Status read clears ONLY cmd_pending, SOVR and SCOL. vint_happened
        // and the CPU-facing IRQ-pending flags are cleared by the CPU's IACK
        // cycle -- clearing them here would drop a pending VINT whenever the
        // game polled status with the IRQ masked.
        cmd_pending_ = false;
        sprite_overflow_ = false;
        sprite_collision_ = false;
        return s;
    }

    // ------------------------------------------------------------------------
    //  68000-facing MMIO
    // ------------------------------------------------------------------------

    std::uint16_t genesis_vdp::read16(std::uint32_t offset) noexcept {
        offset &= 0x1EU;
        switch (offset) {
        case 0x00:
        case 0x02:
            return data_read();
        case 0x04:
        case 0x06:
            return status_read();
        case 0x08:
        case 0x0A:
        case 0x0C:
        case 0x0E:
            return (hv_latch_enabled() && hv_latched_) ? hv_latch_value_ : hv_counter_live();
        default:
            return 0xFFFFU; // test/debug registers are not modelled
        }
    }

    void genesis_vdp::write16(std::uint32_t offset, std::uint16_t value) noexcept {
        offset &= 0x1EU;
        switch (offset) {
        case 0x00:
        case 0x02:
            data_write(value);
            break;
        case 0x04:
        case 0x06:
            ctrl_write(value);
            break;
        default:
            break; // test/debug registers are not modelled
        }
        refresh_irq();
    }

    std::uint8_t genesis_vdp::read8(std::uint32_t offset) noexcept {
        const std::uint16_t word = read16(offset & ~1U);
        return (offset & 1U) != 0U ? static_cast<std::uint8_t>(word)
                                   : static_cast<std::uint8_t>(word >> 8U);
    }

    void genesis_vdp::write8(std::uint32_t offset, std::uint8_t value) noexcept {
        // Byte writes mirror the value into both halves of the word.
        write16(offset, static_cast<std::uint16_t>((value << 8U) | value));
    }

    // ------------------------------------------------------------------------
    //  Rendering
    // ------------------------------------------------------------------------

    int genesis_vdp::hsize_cells() const noexcept { return scroll_size(reg_[16] & 0x03); }
    int genesis_vdp::vsize_cells() const noexcept { return scroll_size((reg_[16] >> 4U) & 0x03); }

    int genesis_vdp::display_line_for_field(int line) const noexcept {
        return interlace_enabled() ? line * 2 + (odd_frame_ ? 1 : 0) : line;
    }

    int genesis_vdp::source_line_for_field(int line) const noexcept {
        if (!interlace_enabled()) {
            return line;
        }
        const int display_line = display_line_for_field(line);
        // Interlace-1 repeats the same 8x8 data on both fields; interlace-2 consumes
        // true 8x16 rows.
        return interlace_mode2() ? display_line : (display_line >> 1);
    }

    std::uint32_t genesis_vdp::cram_to_rgb(std::uint16_t cram_value, std::uint8_t shade) noexcept {
        // CRAM word layout: 0000 BBB0 GGG0 RRR0.
        const int cr4 = channel_intensity((cram_value >> 1U) & 7, shade);
        const int cg4 = channel_intensity((cram_value >> 5U) & 7, shade);
        const int cb4 = channel_intensity((cram_value >> 9U) & 7, shade);
        return (static_cast<std::uint32_t>(pack_rb_8(cr4)) << 16U) |
               (static_cast<std::uint32_t>(pack_g_8(cg4)) << 8U) |
               static_cast<std::uint32_t>(pack_rb_8(cb4));
    }

    void genesis_vdp::fetch_pattern_row(int tile_idx, int row, bool hflip, bool interlace2,
                                        std::uint8_t* pix) const noexcept {
        const std::uint32_t addr = interlace2 ? static_cast<std::uint32_t>(tile_idx & ~1) * 32U +
                                                    static_cast<std::uint32_t>(row & 15) * 4U
                                              : static_cast<std::uint32_t>(tile_idx) * 32U +
                                                    static_cast<std::uint32_t>(row & 7) * 4U;
        const std::array<std::uint8_t, 4> bytes = {
            vram_[(addr + 0U) & 0xFFFFU], vram_[(addr + 1U) & 0xFFFFU],
            vram_[(addr + 2U) & 0xFFFFU], vram_[(addr + 3U) & 0xFFFFU]};
        for (int x = 0; x < 8; ++x) {
            const int src_x = hflip ? (7 - x) : x;
            const std::uint8_t packed = bytes[static_cast<std::size_t>(src_x >> 1)];
            pix[x] =
                static_cast<std::uint8_t>((src_x & 1) == 0 ? (packed >> 4U) : (packed & 0x0FU));
        }
    }

    void genesis_vdp::render_scroll_plane(std::uint32_t nt_base, int line, bool is_plane_a,
                                          std::uint8_t* linebuf) const noexcept {
        const bool il2 = interlace_mode2();
        const int source_line = source_line_for_field(line);
        const int screen_w = visible_width();
        const int hsz_cells = hsize_cells();
        const int hsz_px = hsz_cells * 8;
        const int vsz_px = vsize_cells() * (il2 ? 16 : 8);

        int hscr_offset = 0;
        switch (hscroll_mode()) {
        case 2:
            hscr_offset = (source_line & ~7) * 4; // per 8-line cell
            break;
        case 3:
            hscr_offset = source_line * 4; // per line
            break;
        default:
            hscr_offset = 0; // full-screen
            break;
        }
        const std::uint32_t hscr_addr = hscroll_base() + static_cast<std::uint32_t>(hscr_offset);
        int hscroll = is_plane_a ? static_cast<std::int16_t>(vram_read16(hscr_addr))
                                 : static_cast<std::int16_t>(vram_read16(hscr_addr + 2U));
        hscroll = -hscroll; // register shifts right; we offset the lookup left

        for (int col = 0; col < screen_w; ++col) {
            int vscroll = 0;
            if (vscroll_per_column()) {
                int idx = (col >> 4) * 2 + (is_plane_a ? 0 : 1); // per 2 cells (16 px)
                if (idx < 0 || idx >= vsram_entries) {
                    idx = is_plane_a ? 0 : 1;
                }
                vscroll = vsram_[static_cast<std::size_t>(idx)];
            } else {
                vscroll = vsram_[is_plane_a ? 0U : 1U];
            }

            int px = (col + hscroll) % hsz_px;
            if (px < 0) {
                px += hsz_px;
            }
            // Genesis VDP V scroll convention: visible_line N shows plane row
            // (vscroll + N) mod plane_height. Subtraction is the wrong direction.
            int py = (vscroll + source_line) % vsz_px;
            if (py < 0) {
                py += vsz_px;
            }

            const int cell_x = px >> 3;
            const int cell_y = py >> (il2 ? 4 : 3);
            const int fine_x = px & 7;
            const int fine_y = py & (il2 ? 15 : 7);

            const std::uint32_t nt_offset =
                static_cast<std::uint32_t>(cell_y * hsz_cells + cell_x) * 2U;
            const std::uint16_t nt = vram_read16(nt_base + nt_offset);
            const int tile = nt & 0x7FF;
            const bool hf = ((nt >> 11U) & 1U) != 0U;
            const bool vf = ((nt >> 12U) & 1U) != 0U;
            const int pal = (nt >> 13U) & 3;
            const bool pri = ((nt >> 15U) & 1U) != 0U;

            const int row = vf ? ((il2 ? 15 : 7) - fine_y) : fine_y;
            std::array<std::uint8_t, 8> pix{};
            fetch_pattern_row(tile, row, hf, il2, pix.data());
            const std::uint8_t color = pix[static_cast<std::size_t>(fine_x)];
            linebuf[static_cast<std::size_t>(col)] =
                static_cast<std::uint8_t>((pal * 16 + color) | (pri ? pix_priority : 0));
        }
    }

    void genesis_vdp::render_window(int line, std::uint8_t* linebuf) const noexcept {
        const bool il2 = interlace_mode2();
        const int source_line = source_line_for_field(line);
        const int screen_w = visible_width();
        const int screen_cells = screen_w >> 3;
        const std::uint32_t nt_base = ntw_base();
        const int nt_pitch = h40_mode() ? 64 : 32;

        const int win_h_cell = (reg_[17] & 0x1FU) * 2; // 2-cell granularity
        const int win_v_cell = reg_[18] & 0x1F;
        const bool win_right = (reg_[17] & 0x80U) != 0U;
        const bool win_down = (reg_[18] & 0x80U) != 0U;

        const int cell_row = source_line >> (il2 ? 4 : 3);
        const int fine_y = source_line & (il2 ? 15 : 7);

        // When the V condition covers the line, the window claims the full
        // visible width with no V-scroll, regardless of reg[17] H; otherwise
        // the H boundary splits the line between window and plane A.
        const bool win_takes_line = win_down ? (cell_row >= win_v_cell) : (cell_row < win_v_cell);
        for (int cell_x = 0; cell_x < screen_cells; ++cell_x) {
            const bool in_win_h = win_right ? (cell_x >= win_h_cell) : (cell_x < win_h_cell);
            if (!win_takes_line && !in_win_h) {
                continue;
            }

            const std::uint32_t nt_offset =
                static_cast<std::uint32_t>(cell_row * nt_pitch + cell_x) * 2U;
            const std::uint16_t nt = vram_read16(nt_base + nt_offset);
            const int tile = nt & 0x7FF;
            const bool hf = ((nt >> 11U) & 1U) != 0U;
            const bool vf = ((nt >> 12U) & 1U) != 0U;
            const int pal = (nt >> 13U) & 3;
            const bool pri = ((nt >> 15U) & 1U) != 0U;

            const int row = vf ? ((il2 ? 15 : 7) - fine_y) : fine_y;
            std::array<std::uint8_t, 8> pix{};
            fetch_pattern_row(tile, row, hf, il2, pix.data());

            const int base_x = cell_x * 8;
            for (int p = 0; p < 8; ++p) {
                const int x = base_x + p;
                if (x < screen_w) {
                    linebuf[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(
                        (pal * 16 + pix[static_cast<std::size_t>(p)]) | (pri ? pix_priority : 0));
                }
            }
        }
    }

    void genesis_vdp::render_sprites(int line, std::uint8_t* linebuf,
                                     std::uint8_t* shade) noexcept {
        const bool il2 = interlace_mode2();
        const bool il = interlace_enabled();
        const int display_line = display_line_for_field(line);
        const int screen_w = visible_width();
        const int max_sprites_per_line = h40_mode() ? 20 : 16;
        const int max_pixels_per_line = screen_w;
        const std::uint32_t sat = sat_base();

        int sprites_on_line = 0;
        int pixels_on_line = 0;
        int link = 0;
        bool mask_active = false;
        bool stop_render = false;

        std::array<std::uint8_t, fb_width> spr_buf{};
        std::array<std::uint8_t, fb_width> spr_taken{};

        for (int s = 0; s < sprites_max; ++s) {
            const std::uint32_t sat_addr = sat + static_cast<std::uint32_t>(link) * 8U;
            const std::uint16_t w0 = vram_read16(sat_addr + 0U);
            const std::uint16_t w1 = vram_read16(sat_addr + 2U);
            const std::uint16_t w2 = vram_read16(sat_addr + 4U);
            const std::uint16_t w3 = vram_read16(sat_addr + 6U);

            const int spr_y = (w0 & 0x03FF) - 128;
            const int spr_h = ((w1 >> 8U) & 3) + 1;  // height in cells
            const int spr_w = ((w1 >> 10U) & 3) + 1; // width in cells
            const int next = w1 & 0x7F;

            const int tile = w2 & 0x07FF;
            const bool hf = ((w2 >> 11U) & 1U) != 0U;
            const bool vf = ((w2 >> 12U) & 1U) != 0U;
            const int pal = (w2 >> 13U) & 3;
            const bool pri = ((w2 >> 15U) & 1U) != 0U;

            const int raw_x = w3 & 0x03FF;
            const int spr_x = raw_x - 128;
            const int spr_h_px = spr_h * (il ? 16 : 8);

            if (display_line >= spr_y && display_line < spr_y + spr_h_px) {
                bool render_masked = mask_active;

                ++sprites_on_line;
                if (sprites_on_line > max_sprites_per_line) {
                    sprite_overflow_ = true;
                    break;
                }

                // Raw X=0 masks the rest of the line once another sprite is present.
                if (raw_x == 0 && sprites_on_line > 1) {
                    render_masked = true;
                    mask_active = true;
                }

                int row_in_sprite = display_line - spr_y;
                if (vf) {
                    row_in_sprite = spr_h_px - 1 - row_in_sprite;
                }
                const int source_row =
                    il2 ? row_in_sprite : (il ? (row_in_sprite >> 1) : row_in_sprite);
                const int cell_row = source_row >> (il2 ? 4 : 3);
                const int fine_row = source_row & (il2 ? 15 : 7);

                for (int cx = 0; cx < spr_w; ++cx) {
                    const int actual_cx = hf ? (spr_w - 1 - cx) : cx;
                    // Patterns are column-major within a sprite.
                    const int cell_tile = tile + actual_cx * spr_h + cell_row;
                    std::array<std::uint8_t, 8> pix{};
                    fetch_pattern_row(cell_tile, fine_row, hf, il2, pix.data());

                    for (int p = 0; p < 8; ++p) {
                        const int x = spr_x + cx * 8 + p;
                        if (x < 0 || x >= screen_w) {
                            continue;
                        }
                        if (pix[static_cast<std::size_t>(p)] == 0) {
                            continue; // transparent
                        }

                        ++pixels_on_line;
                        if (pixels_on_line > max_pixels_per_line) {
                            sprite_overflow_ = true;
                            stop_render = true;
                            break;
                        }
                        if (render_masked) {
                            continue;
                        }

                        const auto xi = static_cast<std::size_t>(x);
                        if (spr_taken[xi] != 0) {
                            sprite_collision_ = true;
                            continue;
                        }
                        spr_taken[xi] = 1;

                        const std::uint8_t color = pix[static_cast<std::size_t>(p)];
                        const bool special_highlight = sh_enabled() && pal == 3 && color == 14;
                        const bool special_shadow = sh_enabled() && pal == 3 && color == 15;
                        const bool bg_pri = (linebuf[xi] & pix_priority) != 0;
                        const bool bg_opaque = (linebuf[xi] & 0x0F) != 0;
                        const bool sprite_visible = pri || !bg_pri || !bg_opaque;

                        if (special_highlight || special_shadow) {
                            if (sprite_visible && (linebuf[xi] & pix_source_sprite) == 0) {
                                shade[xi] = special_highlight
                                                ? (shade[xi] == shade_shadow ? shade_normal
                                                                             : shade_highlight)
                                                : (shade[xi] == shade_highlight ? shade_normal
                                                                                : shade_shadow);
                            }
                            continue;
                        }

                        spr_buf[xi] = static_cast<std::uint8_t>(
                            (pal * 16 + color) | (pri ? pix_priority : 0) | pix_source_sprite);
                    }
                    if (stop_render) {
                        break;
                    }
                }
                if (stop_render) {
                    break;
                }
            }

            link = next;
            if (link == 0) {
                break;
            }
        }

        // Merge sprites over the planes using priority rules.
        for (int x = 0; x < screen_w; ++x) {
            const auto xi = static_cast<std::size_t>(x);
            if (spr_buf[xi] == 0) {
                continue;
            }
            const bool spr_pri = (spr_buf[xi] & pix_priority) != 0;
            const bool bg_pri = (linebuf[xi] & pix_priority) != 0;
            const bool bg_opaque = (linebuf[xi] & 0x0F) != 0;
            if (spr_pri || !bg_pri || !bg_opaque) {
                linebuf[xi] = spr_buf[xi];
                if (!sh_enabled() || spr_pri || shade[xi] != shade_shadow) {
                    shade[xi] = shade_normal;
                }
            }
        }
    }

    void genesis_vdp::fill_line_background(int display_line) noexcept {
        const std::uint32_t rgb =
            cram_to_rgb(cram_[static_cast<std::size_t>(bg_index()) & 0x3FU], shade_normal);
        const std::size_t base = static_cast<std::size_t>(display_line) * fb_width;
        for (int x = 0; x < fb_width; ++x) {
            framebuffer_[base + static_cast<std::size_t>(x)] = rgb;
        }
    }

    void genesis_vdp::render_scanline(int line) noexcept {
        const int display_line = display_line_for_field(line);
        const int screen_w = visible_width();

        std::array<std::uint8_t, fb_width> plane_b{};
        std::array<std::uint8_t, fb_width> plane_a{};
        render_scroll_plane(ntb_base(), line, false, plane_b.data());
        render_scroll_plane(nta_base(), line, true, plane_a.data());
        render_window(line, plane_a.data());

        std::array<std::uint8_t, fb_width> composited{};
        std::array<std::uint8_t, fb_width> shade{};
        const auto bg = static_cast<std::uint8_t>(bg_index());
        for (int x = 0; x < screen_w; ++x) {
            const auto xi = static_cast<std::size_t>(x);
            const bool a_opaque = (plane_a[xi] & 0x0F) != 0;
            const bool b_opaque = (plane_b[xi] & 0x0F) != 0;
            const bool a_pri = (plane_a[xi] & pix_priority) != 0;
            const bool b_pri = (plane_b[xi] & pix_priority) != 0;
            shade[xi] = shade_normal;

            if (a_pri && a_opaque) {
                composited[xi] = plane_a[xi];
            } else if (b_pri && b_opaque) {
                composited[xi] = plane_b[xi];
            } else if (a_opaque) {
                composited[xi] = plane_a[xi];
                if (sh_enabled()) {
                    shade[xi] = shade_shadow;
                }
            } else if (b_opaque) {
                composited[xi] = plane_b[xi];
            } else {
                composited[xi] = bg;
            }
        }

        render_sprites(line, composited.data(), shade.data());

        const std::size_t fb_base = static_cast<std::size_t>(display_line) * fb_width;
        for (int x = 0; x < screen_w; ++x) {
            const auto xi = static_cast<std::size_t>(x);
            const std::uint16_t color = cram_[composited[xi] & 0x3FU];
            framebuffer_[fb_base + xi] = cram_to_rgb(color, shade[xi]);
        }

        // Blank the leftmost 8 pixels (R0 bit 5).
        if (blank_left()) {
            const std::uint32_t bgc =
                cram_to_rgb(cram_[static_cast<std::size_t>(bg_index()) & 0x3FU], shade_normal);
            for (int x = 0; x < 8 && x < screen_w; ++x) {
                framebuffer_[fb_base + static_cast<std::size_t>(x)] = bgc;
            }
        }

        // Fill the H32 right margin (256-319) with the backdrop.
        if (screen_w < fb_width) {
            const std::uint32_t bgc =
                cram_to_rgb(cram_[static_cast<std::size_t>(bg_index()) & 0x3FU], shade_normal);
            for (int x = screen_w; x < fb_width; ++x) {
                framebuffer_[fb_base + static_cast<std::size_t>(x)] = bgc;
            }
        }
    }

    // ------------------------------------------------------------------------
    //  Timing + interrupts
    // ------------------------------------------------------------------------

    void genesis_vdp::run_scanline() noexcept {
        const int visible_h = field_height();
        const int visible_w = visible_width();

        in_hblank_ = false;
        hcounter_ = visible_w / 2;

        if (scanline_ < visible_h) {
            in_vblank_ = false;
            if (display_enabled()) {
                render_scanline(scanline_);
            } else {
                fill_line_background(display_line_for_field(scanline_));
            }
            in_hblank_ = true;
            hcounter_ = visible_w;
            if (hint_counter_ <= 0) {
                hint_counter_ = reg_[10];
                if (hint_enabled()) {
                    hblank_pending_ = true;
                }
            } else {
                --hint_counter_;
            }
        } else if (scanline_ == visible_h) {
            // First VBL line. The VBL-entry state (in_vblank_,
            // vint_happened_, VINT delay, odd_frame_ flip, frame_index_++)
            // was set at the scanline-becomes-visible_h transition below,
            // BEFORE this line's 3420 master cycles accumulated -- so VINT
            // fires at master 788 of THIS line, not 788 after it completes.
            in_hblank_ = true;
            hcounter_ = visible_w;
            if (hint_counter_ <= 0) {
                hint_counter_ = reg_[10];
                if (hint_enabled()) {
                    hblank_pending_ = true;
                }
            } else {
                --hint_counter_;
            }
        } else {
            // V-blank lines past the entry line: the HINT counter is held at R10
            // and does not fire. Decrement runs only through the active display
            // and the first V-blank line; without this hold, games that drive
            // raster effects off HINT with R10 < V-blank-lines see spurious
            // extra interrupts each frame.
            in_hblank_ = true;
            hcounter_ = visible_w;
            hint_counter_ = reg_[10];
        }

        ++scanline_;
        if (scanline_ >= total_scanlines_) {
            scanline_ = 0;
            hint_counter_ = reg_[10];
        }

        // VBL entry happens at the scanline-becomes-visible_h transition
        // (before this line's master cycles accumulate). This positions the
        // VINT delay countdown at master 0 of the VBL line so the IRQ
        // fires at master 788 (H40) / 770 (H32) of that same line.
        if (scanline_ == visible_h) {
            in_vblank_ = true;
            vint_happened_ = true;
            odd_frame_ = interlace_enabled() ? !odd_frame_ : false;
            // Start the VINT delay UNCONDITIONALLY; the vint_enabled() gate
            // is applied at drain-completion in tick(). Gating here would
            // skip the first VINT for games that enable V-int within the
            // 770/788-cycle delay window.
            vint_pending_delay_master_ = h40_mode() ? 788 : 770;
            ++frame_index_;
        }

        vcounter_ = vcounter_readback(scanline_, odd_frame_);
        in_hblank_ = false;
        hcounter_ = visible_width() / 2;
    }

    void genesis_vdp::refresh_irq() noexcept {
        const int level = pending_irq_level();
        if (level != last_irq_level_) {
            last_irq_level_ = level;
            if (irq_callback_) {
                irq_callback_(level);
            }
        }
        // The Genesis Z80's INT line tracks vblank on real hardware; the manifest
        // wires set_vblank_callback to s->z80.set_irq_line so sound drivers tick.
        if (in_vblank_ != last_in_vblank_) {
            last_in_vblank_ = in_vblank_;
            if (vblank_callback_) {
                vblank_callback_(in_vblank_);
            }
        }
    }

    void genesis_vdp::tick(std::uint64_t cycles) {
        line_accumulator_ += static_cast<std::int64_t>(cycles);
        while (line_accumulator_ >= master_clocks_per_line) {
            line_accumulator_ -= master_clocks_per_line;
            run_scanline();
            // The FIFO drain schedule is kept in the current line's frame of
            // reference; shift each entry back one line as the line wraps so
            // entries that drained "into the next line" stay comparable to the
            // new line's within-line position. Clamp at 0 (already drained).
            for (auto& d : fifo_drain_) {
                d -= master_clocks_per_line;
                if (d < 0) {
                    d = 0;
                }
            }
            fifo_accept_cursor_master_ -= master_clocks_per_line;
            if (fifo_accept_cursor_master_ < 0) {
                fifo_accept_cursor_master_ = 0;
            }
        }
        // Drain the post-VBLANK-entry VINT delay: VBLANK status went high
        // when scanline crossed visible_h, but the IRQ doesn't assert until
        // the delay window elapses.
        if (vint_pending_delay_master_ > 0) {
            vint_pending_delay_master_ -= static_cast<std::int64_t>(cycles);
            if (vint_pending_delay_master_ <= 0) {
                vint_pending_delay_master_ = -1;
                ++vint_drain_count_; // diagnostic
                if (vint_enabled()) {
                    ++vint_enabled_at_drain_count_; // diagnostic
                    if (!vblank_pending_) {
                        ++vint_fired_count_; // diagnostic: count rising edges
                    }
                    vblank_pending_ = true;
                }
            }
        }
        // Drain any pending DMA stall debt; while > 0 the genesis_system gates
        // the 68000 off so the bus appears held to the CPU during DMA, matching
        // real hardware's behaviour.
        if (dma_stall_master_cycles_ > 0) {
            dma_stall_master_cycles_ -= static_cast<std::int64_t>(cycles);
            if (dma_stall_master_cycles_ < 0) {
                dma_stall_master_cycles_ = 0;
            }
        }
        // Drain FIFO back-pressure stall debt (same 68K-gating path as DMA).
        if (fifo_stall_master_cycles_ > 0) {
            fifo_stall_master_cycles_ -= static_cast<std::int64_t>(cycles);
            if (fifo_stall_master_cycles_ < 0) {
                fifo_stall_master_cycles_ = 0;
            }
        }
        // Drain the DMA-busy *status bit* timer (independent: VRAM fill/copy
        // hold busy=1 for the DMA duration but don't lock the 68K).
        if (dma_busy_master_cycles_ > 0) {
            dma_busy_master_cycles_ -= static_cast<std::int64_t>(cycles);
            if (dma_busy_master_cycles_ <= 0) {
                dma_busy_master_cycles_ = 0;
                dma_busy_ = false;
            }
        }
        refresh_irq();
    }

    std::int64_t genesis_vdp::estimate_dma_transfer_cycles(std::uint32_t length_units,
                                                           int dma_type) const noexcept {
        // DMA-rate model. Per-line access slots:
        //                 H32  H40
        //   active        16    18
        //   blanking     166   204
        // Adjustments:
        //   dma_type & 1 (type 1=68K->VRAM, type 3=VRAM copy):
        //       rate >>= 1   (one word = 2 access slots)
        //   dma_type == 0 (68K->CRAM/VSRAM) with display off:
        //       rate -= 5 (H32) or 6 (H40)   (refresh slots cost one each)
        // Stall master cycles = length_units * MCYCLES_PER_LINE / rate.
        const bool blanking = in_vblank_ || !display_enabled();
        constexpr int kMCyclesPerLine = 3420;
        const bool h40 = h40_mode();
        int rate = blanking ? (h40 ? 204 : 166) : (h40 ? 18 : 16);
        if (dma_type & 1) {
            rate >>= 1; // one VRAM/copy word = 2 access slots
        }
        if (dma_type == 0 && blanking) {
            // Refresh slots: 5/line in H32, 6/line in H40, cost 1 slot each.
            rate -= h40 ? 6 : 5;
        }
        if (rate <= 0) {
            rate = 1; // belt-and-suspenders; avoids div-by-zero on garbage state
        }
        return (static_cast<std::int64_t>(length_units) * kMCyclesPerLine) / rate;
    }

    int genesis_vdp::pending_irq_level() const noexcept {
        if (vblank_pending_) {
            return 6;
        }
        if (hblank_pending_) {
            return 4;
        }
        if (ext_pending_) {
            return 2;
        }
        return 0;
    }

    void genesis_vdp::signal_external_int() noexcept {
        if (ext_int_enabled()) {
            ext_pending_ = true;
        }
        refresh_irq();
    }

    void genesis_vdp::acknowledge_irq(int level) noexcept {
        // IACK clears the pending latch for the accepted level.
        if (level >= 6) {
            vblank_pending_ = false;
            vint_happened_ = false;
        } else if (level >= 4) {
            hblank_pending_ = false;
        } else if (level >= 2) {
            ext_pending_ = false;
        }
        // The delayed-IRQ path raises CPU irq_level_ directly without going
        // through refresh_irq, so VDP's last_irq_level_ stayed 0 while the
        // CPU was at 6. Force last_irq_level_ to the acked level so the
        // following refresh_irq's "if changed" guard fires and deasserts
        // the CPU IRQ -- otherwise the CPU spins on a never-cleared IRQ.
        last_irq_level_ = level;
        refresh_irq();
    }

    void genesis_vdp::set_pal(bool pal) noexcept {
        pal_mode_ = pal;
        total_scanlines_ = pal ? scanlines_pal : scanlines_ntsc;
    }

    // ------------------------------------------------------------------------
    //  Lifecycle + state
    // ------------------------------------------------------------------------

    void genesis_vdp::reset(reset_kind /*kind*/) {
        vram_.fill(0);
        cram_.fill(0);
        vsram_.fill(0);
        reg_.fill(0);

        cmd_addr_ = 0;
        cmd_code_ = 0;
        cmd_pending_ = false;
        cmd_first_ = 0;
        read_buffer_ = 0;

        dma_fill_pending_ = false;
        dma_fill_byte_ = 0;
        dma_fill_code_ = 0;
        dma_fill_word_ = 0;
        dma_source_ = 0;
        dma_busy_ = false;
        dma_busy_master_cycles_ = 0;
        dma_stall_master_cycles_ = 0;

        fifo_drain_.fill(0);
        fifo_idx_ = 0;
        fifo_stall_master_cycles_ = 0;
        fifo_accept_cursor_master_ = 0;

        total_scanlines_ = pal_mode_ ? scanlines_pal : scanlines_ntsc;
        // Frame phase: each frame starts on the VBL entry line so the very
        // first VINT after reset fires at master 770/788 -- not after a
        // whole active display has scrolled past first.
        scanline_ = field_height();
        hcounter_ = visible_width() / 2;
        vcounter_ = vcounter_readback(scanline_, false);
        odd_frame_ = false;
        hint_counter_ = 0;
        hv_latched_ = false;
        hv_latch_value_ = 0;
        line_accumulator_ = 0;

        vint_happened_ = false;
        // Prime the delay so the first VINT fires at master 770 (H32 reset
        // default) / 788 (H40) of the first frame.
        vint_pending_delay_master_ = h40_mode() ? 788 : 770;
        sprite_overflow_ = false;
        sprite_collision_ = false;
        in_vblank_ = true; // start at the VBL entry line
        in_hblank_ = false;
        vblank_pending_ = false;
        hblank_pending_ = false;
        ext_pending_ = false;

        framebuffer_.assign(static_cast<std::size_t>(fb_width) * fb_height, 0U);
        frame_index_ = 0;
        last_irq_level_ = 0;
    }

    frame_buffer_view genesis_vdp::framebuffer() const noexcept {
        // The framebuffer storage is always fb_width pixels per row (320, sized
        // for the worst-case H40 + interlace), but the *visible* width depends
        // on the active mode -- H32 renders only 256 columns and leaves the
        // remaining 64 columns untouched (containing whatever the previous
        // mode left there). Report the visible width as `width` and the
        // storage pitch as `stride` so consumers don't display the stale tail.
        return {
            .pixels = framebuffer_.data(),
            .width = static_cast<std::uint32_t>(visible_width()),
            .height = static_cast<std::uint32_t>(visible_height()),
            .stride = static_cast<std::uint32_t>(fb_width),
        };
    }

    std::uint16_t genesis_vdp::vram16(std::uint32_t addr) const noexcept {
        return vram_read16(addr);
    }

    std::uint16_t genesis_vdp::vsram(int idx) const noexcept {
        return (idx >= 0 && idx < vsram_entries) ? vsram_[static_cast<std::size_t>(idx)]
                                                 : std::uint16_t{0};
    }

    void genesis_vdp::save_state(state_writer& writer) const {
        writer.bytes(vram_);
        for (const auto c : cram_) {
            writer.u16(c);
        }
        for (const auto v : vsram_) {
            writer.u16(v);
        }
        writer.bytes(reg_);

        writer.u32(cmd_addr_);
        writer.u8(cmd_code_);
        writer.boolean(cmd_pending_);
        writer.u16(cmd_first_);
        writer.u16(read_buffer_);

        writer.boolean(dma_fill_pending_);
        writer.u8(dma_fill_byte_);
        writer.u8(dma_fill_code_);
        writer.u16(dma_fill_word_);
        writer.u32(dma_source_);
        writer.boolean(dma_busy_);

        writer.u32(static_cast<std::uint32_t>(scanline_));
        writer.u32(static_cast<std::uint32_t>(hcounter_));
        writer.u32(static_cast<std::uint32_t>(vcounter_));
        writer.u32(static_cast<std::uint32_t>(total_scanlines_));
        writer.boolean(pal_mode_);
        writer.boolean(odd_frame_);
        writer.u32(static_cast<std::uint32_t>(hint_counter_));
        writer.boolean(hv_latched_);
        writer.u16(hv_latch_value_);
        writer.u64(static_cast<std::uint64_t>(line_accumulator_));

        writer.boolean(vint_happened_);
        writer.boolean(sprite_overflow_);
        writer.boolean(sprite_collision_);
        writer.boolean(in_vblank_);
        writer.boolean(in_hblank_);
        writer.boolean(vblank_pending_);
        writer.boolean(hblank_pending_);
        writer.boolean(ext_pending_);
        writer.u64(frame_index_);

        // Write-FIFO schedule + stall debt: part of the VDP's live timing state.
        // Serialized so a save taken mid active-display burst restores the same
        // back-pressure on load (deterministic save->load->continue).
        for (const auto d : fifo_drain_) {
            writer.u64(static_cast<std::uint64_t>(d));
        }
        writer.u32(static_cast<std::uint32_t>(fifo_idx_));
        writer.u64(static_cast<std::uint64_t>(fifo_stall_master_cycles_));
        // fifo_accept_cursor_master_ is opt-in (MNEMOS_WRITE_ACCEPT) transient state and
        // is deliberately NOT in the serialized format -- keeps the chunk byte-compatible.
    }

    void genesis_vdp::load_state(state_reader& reader) {
        reader.bytes(vram_);
        for (auto& c : cram_) {
            c = reader.u16();
        }
        for (auto& v : vsram_) {
            v = reader.u16();
        }
        reader.bytes(reg_);

        cmd_addr_ = reader.u32();
        cmd_code_ = reader.u8();
        cmd_pending_ = reader.boolean();
        cmd_first_ = reader.u16();
        read_buffer_ = reader.u16();

        dma_fill_pending_ = reader.boolean();
        dma_fill_byte_ = reader.u8();
        dma_fill_code_ = reader.u8();
        dma_fill_word_ = reader.u16();
        dma_source_ = reader.u32();
        dma_busy_ = reader.boolean();

        scanline_ = static_cast<int>(reader.u32());
        hcounter_ = static_cast<int>(reader.u32());
        vcounter_ = static_cast<int>(reader.u32());
        total_scanlines_ = static_cast<int>(reader.u32());
        pal_mode_ = reader.boolean();
        odd_frame_ = reader.boolean();
        hint_counter_ = static_cast<int>(reader.u32());
        hv_latched_ = reader.boolean();
        hv_latch_value_ = reader.u16();
        line_accumulator_ = static_cast<std::int64_t>(reader.u64());

        vint_happened_ = reader.boolean();
        sprite_overflow_ = reader.boolean();
        sprite_collision_ = reader.boolean();
        in_vblank_ = reader.boolean();
        in_hblank_ = reader.boolean();
        vblank_pending_ = reader.boolean();
        hblank_pending_ = reader.boolean();
        ext_pending_ = reader.boolean();
        frame_index_ = reader.u64();

        for (auto& d : fifo_drain_) {
            d = static_cast<std::int64_t>(reader.u64());
        }
        fifo_idx_ = static_cast<int>(reader.u32());
        fifo_stall_master_cycles_ = static_cast<std::int64_t>(reader.u64());
        // Not serialized (opt-in transient state): reset deterministically on load;
        // re-derives at the next scanline wrap when the accept path is active.
        fifo_accept_cursor_master_ = 0;

        last_irq_level_ = pending_irq_level();
    }

    instrumentation::ichip_introspection& genesis_vdp::introspection() noexcept {
        return introspection_;
    }

    void genesis_vdp::configure(const config_table& cfg, const callback_table& callbacks) {
        // PAL (50 Hz, 313 scanlines) vs NTSC (60 Hz, 262 scanlines). The
        // Sega Genesis manifest sets `pal = true` for PAL ROMs; defaults to
        // NTSC.
        if (const auto v = chips::cfg_bool(cfg, "pal")) {
            set_pal(*v);
        }

        // The VDP's host hooks: dma_read (68K->VDP DMA word source), irq
        // (CPU IRQ assert/clear), delayed_irq (one-instruction-delayed IRQ
        // for the canonical V-int-enable-via-MOVE.W path), and vblank (Z80
        // IRQ-on-VBLANK edge signal). Phase A.2 plumbing in place; Phase B
        // wires these through the manifest. assemble_genesis still installs
        // them inline today.
        if (const auto id = chips::cfg_string(cfg, "dma_read_callback")) {
            if (const auto* fn =
                    chips::find_callback<std::uint16_t(std::uint32_t)>(callbacks, *id)) {
                set_dma_read(*fn);
            }
        }
        if (const auto id = chips::cfg_string(cfg, "irq_callback")) {
            if (const auto* fn = chips::find_callback<void(int)>(callbacks, *id)) {
                set_irq_callback(*fn);
            }
        }
        if (const auto id = chips::cfg_string(cfg, "delayed_irq_callback")) {
            if (const auto* fn = chips::find_callback<void(int)>(callbacks, *id)) {
                set_delayed_irq_callback(*fn);
            }
        }
        if (const auto id = chips::cfg_string(cfg, "vblank_callback")) {
            if (const auto* fn = chips::find_callback<void(bool)>(callbacks, *id)) {
                set_vblank_callback(*fn);
            }
        }
    }

    std::span<const register_descriptor> genesis_vdp::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"MODE1", reg_[0], 8U, fmt::flags};
        register_view_[1] = {"MODE2", reg_[1], 8U, fmt::flags};
        register_view_[2] = {"MODE3", reg_[11], 8U, fmt::flags};
        register_view_[3] = {"MODE4", reg_[12], 8U, fmt::flags};
        register_view_[4] = {"AUTOINC", reg_[15], 8U, fmt::unsigned_integer};
        register_view_[5] = {"CMD_ADDR", cmd_addr_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"CMD_CODE", cmd_code_, 6U, fmt::flags};
        register_view_[7] = {"DMA_LEN", dma_length(), 16U, fmt::unsigned_integer};
        register_view_[8] = {"DMA_SRC", dma_source(), 23U, fmt::unsigned_integer};
        register_view_[9] = {"SCANLINE", static_cast<std::uint64_t>(scanline_), 16U,
                             fmt::unsigned_integer};
        register_view_[10] = {"VCOUNTER", static_cast<std::uint64_t>(vcounter_), 8U,
                              fmt::unsigned_integer};
        register_view_[11] = {"IRQ_LEVEL", static_cast<std::uint64_t>(pending_irq_level()), 3U,
                              fmt::unsigned_integer};
        register_view_[12] = {"DMA_BUSY", dma_busy_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[13] = {"IN_VBLANK", in_vblank_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[14] = {"FRAME", frame_index_, 64U, fmt::unsigned_integer};
        register_view_[15] = {"FIFO_EMPTY", 1U, 1U, fmt::flags};
        return register_view_;
    }

    // ------------------------------------------------------------------------
    //  Introspection surface
    // ------------------------------------------------------------------------

    namespace {
        [[nodiscard]] int scroll_size_cells(int sz_bits) noexcept {
            constexpr int sizes[4] = {32, 64, 64, 128};
            return sizes[sz_bits & 3];
        }
    } // namespace

    genesis_vdp::introspection_surface::introspection_surface(genesis_vdp& owner) noexcept
        : vram_view_("vram", std::span<const std::uint8_t>(owner.vram_)),
          cram_view_("cram", std::span<const std::uint8_t>(
                                 reinterpret_cast<const std::uint8_t*>(owner.cram_.data()),
                                 owner.cram_.size() * sizeof(std::uint16_t))),
          vsram_view_("vsram", std::span<const std::uint8_t>(
                                   reinterpret_cast<const std::uint8_t*>(owner.vsram_.data()),
                                   owner.vsram_.size() * sizeof(std::uint16_t))),
          regs_view_("vdpregs", std::span<const std::uint8_t>(owner.reg_)), registers_impl_(owner),
          plane_a_(owner) {
        mem_table_[0] = &vram_view_;
        mem_table_[1] = &cram_view_;
        mem_table_[2] = &vsram_view_;
        mem_table_[3] = &regs_view_;
        layer_table_[0] = &plane_a_;
    }

    std::span<const register_descriptor>
    genesis_vdp::introspection_surface::registers_impl::registers() {
        return owner_->register_snapshot();
    }

    frame_buffer_view genesis_vdp::introspection_surface::plane_a_layer_impl::view() const {
        // Lazily render plane A from the chip's live VRAM/CRAM. Pattern is
        // pulled into a chip-owned buffer; geometry tracks the current plane-
        // size registers, which may change between calls. This is the same
        // walk the player's --screenshot path performed in apps/player/main.cpp
        // before the introspection retrofit -- now the chip exposes it
        // directly via the debug_layer surface.
        const int hsz_cells = scroll_size_cells(owner_->reg_[16] & 0x03);
        const int vsz_cells = scroll_size_cells((owner_->reg_[16] >> 4) & 0x03);
        const std::uint32_t nt_base = (static_cast<std::uint32_t>(owner_->reg_[2]) & 0x38U) << 10U;
        const std::uint32_t plane_w = static_cast<std::uint32_t>(hsz_cells * 8);
        const std::uint32_t plane_h = static_cast<std::uint32_t>(vsz_cells * 8);
        const std::size_t total = static_cast<std::size_t>(plane_w) * plane_h;
        buf_.assign(total, 0U);
        width_ = plane_w;
        height_ = plane_h;

        for (int cy = 0; cy < vsz_cells; ++cy) {
            for (int cx = 0; cx < hsz_cells; ++cx) {
                const std::uint32_t nt_offset =
                    static_cast<std::uint32_t>(cy * hsz_cells + cx) * 2U;
                const std::uint16_t nt = owner_->vram16(nt_base + nt_offset);
                const int tile = nt & 0x7FF;
                const bool hf = ((nt >> 11) & 1) != 0;
                const bool vf = ((nt >> 12) & 1) != 0;
                const int pal = (nt >> 13) & 3;
                for (int fy = 0; fy < 8; ++fy) {
                    const int row = vf ? (7 - fy) : fy;
                    for (int fx = 0; fx < 8; ++fx) {
                        const int col = hf ? (7 - fx) : fx;
                        const std::uint32_t pat_addr =
                            static_cast<std::uint32_t>(tile) * 32U + row * 4U + (col / 2);
                        const std::uint16_t word = owner_->vram16(pat_addr & ~1U);
                        const std::uint8_t byte =
                            static_cast<std::uint8_t>((pat_addr & 1) ? (word & 0xFF) : (word >> 8));
                        const std::uint8_t color = (col & 1) ? (byte & 0xF) : (byte >> 4);
                        const std::uint16_t cram_value =
                            owner_->cram_[static_cast<std::size_t>(pal * 16 + color) & 0x3FU];
                        const std::uint32_t rgb =
                            color == 0 ? 0U : cram_to_rgb(cram_value, shade_normal);
                        buf_[(static_cast<std::size_t>(cy) * 8U + fy) * plane_w +
                             (static_cast<std::size_t>(cx) * 8U + fx)] = rgb;
                    }
                }
            }
        }

        return {.pixels = buf_.data(), .width = width_, .height = height_, .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto genesis_vdp_registration =
            register_factory("sega.315_5313", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<genesis_vdp>();
            });
    } // namespace

} // namespace mnemos::chips::video
