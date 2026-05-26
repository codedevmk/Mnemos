#include "genesis_vdp.hpp"

#include "chip_registry.hpp"
#include "genesis_vdp_hcounter_tables.hpp"
#include "state.hpp"

#include <cstddef>
#include <memory>

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

    int genesis_vdp::field_height() const noexcept { return (v30_mode() && pal_mode_) ? 240 : 224; }

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
                dma_fill_pending_ = true;
                dma_busy_ = true;
            } else if (type == 3) {
                // VRAM copy (runs immediately in this functional model).
                std::uint16_t len = dma_length();
                if (len == 0U) {
                    len = 0xFFFFU;
                }
                dma_source_ = static_cast<std::uint32_t>(reg_[21]) |
                              (static_cast<std::uint32_t>(reg_[22]) << 8U);
                dma_busy_ = true;
                for (std::uint16_t i = 0; i < len; ++i) {
                    dma_copy_step();
                }
                dma_busy_ = false;
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
            dma_busy_ = false;
            reg_[19] = 0U;
            reg_[20] = 0U;
            reg_[21] = static_cast<std::uint8_t>(fill_src);
            reg_[22] = static_cast<std::uint8_t>(fill_src >> 8U);
            reg_[23] = static_cast<std::uint8_t>((reg_[23] & 0x80U) | ((fill_src >> 16U) & 0x7FU));
            read_buffer_ = value;
            return;
        }

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
        const bool status_vblank = in_vblank_ || !display_enabled();

        s |= (1U << 9U); // FIFO empty (writes complete immediately in this model)
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
        if (status_vblank) {
            s |= (1U << 3U);
        }
        if (in_hblank_) {
            s |= (1U << 2U);
        }
        if (dma_busy_) {
            s |= (1U << 1U);
        }
        if (pal_mode_) {
            s |= (1U << 0U);
        }

        // Reading status acknowledges the latched VDP interrupt sources.
        cmd_pending_ = false;
        vint_happened_ = false;
        vblank_pending_ = false;
        hblank_pending_ = false;
        sprite_overflow_ = false;
        sprite_collision_ = false;
        refresh_irq();
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
    //  Timing + interrupts
    // ------------------------------------------------------------------------

    void genesis_vdp::run_scanline() noexcept {
        const int visible_h = field_height();
        const int visible_w = visible_width();

        in_hblank_ = false;
        hcounter_ = visible_w / 2;

        if (scanline_ < visible_h) {
            in_vblank_ = false;
            // Rendering happens here in phase 2; the framebuffer stays blank for now.
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
            in_vblank_ = true;
            vint_happened_ = true;
            odd_frame_ = interlace_enabled() ? !odd_frame_ : false;
            in_hblank_ = true;
            hcounter_ = visible_w;
            if (vint_enabled()) {
                vblank_pending_ = true;
            }
            if (hint_counter_ <= 0) {
                hint_counter_ = reg_[10];
                if (hint_enabled()) {
                    hblank_pending_ = true;
                }
            } else {
                --hint_counter_;
            }
        } else {
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
        }

        ++scanline_;
        if (scanline_ >= total_scanlines_) {
            scanline_ = 0;
            hint_counter_ = reg_[10];
            ++frame_index_; // a full field/frame has been scanned
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
    }

    void genesis_vdp::tick(std::uint64_t cycles) {
        line_accumulator_ += static_cast<std::int64_t>(cycles);
        while (line_accumulator_ >= master_clocks_per_line) {
            line_accumulator_ -= master_clocks_per_line;
            run_scanline();
        }
        refresh_irq();
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

        total_scanlines_ = pal_mode_ ? scanlines_pal : scanlines_ntsc;
        scanline_ = 0;
        hcounter_ = visible_width() / 2;
        vcounter_ = vcounter_readback(0, false);
        odd_frame_ = false;
        hint_counter_ = 0;
        hv_latched_ = false;
        hv_latch_value_ = 0;
        line_accumulator_ = 0;

        vint_happened_ = false;
        sprite_overflow_ = false;
        sprite_collision_ = false;
        in_vblank_ = true; // start in vblank until the first frame
        in_hblank_ = false;
        vblank_pending_ = false;
        hblank_pending_ = false;
        ext_pending_ = false;

        framebuffer_.assign(static_cast<std::size_t>(fb_width) * fb_height, 0U);
        frame_index_ = 0;
        last_irq_level_ = 0;
    }

    frame_buffer_view genesis_vdp::framebuffer() const noexcept {
        return {
            .pixels = framebuffer_.data(),
            .width = static_cast<std::uint32_t>(fb_width),
            .height = static_cast<std::uint32_t>(visible_height()),
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

        last_irq_level_ = pending_irq_level();
    }

    instrumentation::ichip_introspection& genesis_vdp::introspection() noexcept {
        return introspection_;
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

    namespace {
        [[maybe_unused]] const auto genesis_vdp_registration =
            register_factory("sega.315_5313", chip_class::video, []() -> std::unique_ptr<ichip> {
                return std::make_unique<genesis_vdp>();
            });
    } // namespace

} // namespace mnemos::chips::video
