#include "vdp1.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // Native 0BGR1555 -> 0x00RRGGBB, 5-bit guns expanded to 8 bits.
        [[nodiscard]] constexpr std::uint32_t native_to_rgb(std::uint16_t native) noexcept {
            const auto expand = [](std::uint32_t five) { return (five << 3U) | (five >> 2U); };
            const std::uint32_t r = expand((native >> 0U) & 0x1FU);
            const std::uint32_t g = expand((native >> 5U) & 0x1FU);
            const std::uint32_t b = expand((native >> 10U) & 0x1FU);
            return (r << 16U) | (g << 8U) | b;
        }
    } // namespace

    chip_metadata vdp1::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "vdp1",
            .family = "saturn",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void vdp1::reset(reset_kind /*kind*/) {
        std::ranges::fill(vram_, std::uint8_t{0});
        std::ranges::fill(framebuffers_[0], std::uint8_t{0});
        std::ranges::fill(framebuffers_[1], std::uint8_t{0});
        std::ranges::fill(regs_, std::uint16_t{0});
        draw_buffer_index_ = 0U;
        tvmr_ = fbcr_ = ewdr_ = ewlr_ = ewrr_ = 0U;
        ptmr_ = ptmr_idle;
        pending_ptmr_ = ptmr_idle;
        pending_ptmr_valid_ = false;
        edsr_ = lopr_ = copr_ = 0U;
        state_ = draw_state::idle;
        cmd_ip_ = 0U;
        call_depth_ = 0U;
        call_stack_.fill(0U);
        local_x_ = local_y_ = 0;
        sys_clip_x1_ = sys_clip_y1_ = 0;
        sys_clip_x2_ = static_cast<std::int16_t>(fb_width - 1U);
        sys_clip_y2_ = static_cast<std::int16_t>(fb_height - 1U);
        user_clip_x1_ = user_clip_y1_ = 0;
        user_clip_x2_ = static_cast<std::int16_t>(fb_width - 1U);
        user_clip_y2_ = static_cast<std::int16_t>(fb_height - 1U);
        frame_index_ = 0U;
        std::ranges::fill(rgb_view_, 0U);
    }

    void vdp1::tick(std::uint64_t cycles) {
        // Functional model: when the engine is armed, run the list to
        // completion. Per-command cycle metering is deferred (see header).
        if (cycles == 0U) {
            return;
        }
        if (state_ == draw_state::plotting) {
            run_to_end(static_cast<int>(vram_size / cmd_table_bytes));
        }
    }

    // ---------------- VRAM + framebuffer access ----------------

    std::uint16_t vdp1::read_vram_word(std::uint32_t offset) const noexcept {
        offset &= static_cast<std::uint32_t>(vram_size - 1U);
        const std::uint32_t lo = (offset + 1U) & static_cast<std::uint32_t>(vram_size - 1U);
        return static_cast<std::uint16_t>((std::uint16_t{vram_[offset]} << 8U) |
                                          std::uint16_t{vram_[lo]});
    }

    void vdp1::write_vram_word(std::uint32_t offset, std::uint16_t value) noexcept {
        offset &= static_cast<std::uint32_t>(vram_size - 1U);
        const std::uint32_t lo = (offset + 1U) & static_cast<std::uint32_t>(vram_size - 1U);
        vram_[offset] = static_cast<std::uint8_t>(value >> 8U);
        vram_[lo] = static_cast<std::uint8_t>(value);
    }

    std::uint16_t vdp1::fb_read(std::int32_t x, std::int32_t y) const noexcept {
        if (x < 0 || x >= static_cast<std::int32_t>(framebuffer_width_pixels()) || y < 0 ||
            y >= static_cast<std::int32_t>(fb_height)) {
            return 0U;
        }
        const auto& fb = framebuffers_[draw_buffer_index_];
        if (is_8bpp_framebuffer()) {
            const std::size_t off =
                static_cast<std::size_t>(y) * fb_pitch_bytes + static_cast<std::size_t>(x);
            return fb[off];
        }
        const std::size_t off =
            (static_cast<std::size_t>(y) * fb_width + static_cast<std::size_t>(x)) * 2U;
        return static_cast<std::uint16_t>((std::uint16_t{fb[off]} << 8U) |
                                          std::uint16_t{fb[off + 1U]});
    }

    void vdp1::fb_write(std::int32_t x, std::int32_t y, std::uint16_t pixel) noexcept {
        if (x < 0 || x >= static_cast<std::int32_t>(framebuffer_width_pixels()) || y < 0 ||
            y >= static_cast<std::int32_t>(fb_height)) {
            return;
        }
        auto& fb = framebuffers_[draw_buffer_index_];
        if (is_8bpp_framebuffer()) {
            const std::size_t off =
                static_cast<std::size_t>(y) * fb_pitch_bytes + static_cast<std::size_t>(x);
            fb[off] = static_cast<std::uint8_t>(pixel);
            return;
        }
        const std::size_t off =
            (static_cast<std::size_t>(y) * fb_width + static_cast<std::size_t>(x)) * 2U;
        fb[off] = static_cast<std::uint8_t>(pixel >> 8U);
        fb[off + 1U] = static_cast<std::uint8_t>(pixel);
    }

    frame_buffer_view vdp1::framebuffer() const noexcept {
        // Convert the DISPLAY buffer (the one the companion would scan out) to
        // the engine's 0x00RRGGBB packing. 16bpp source rows only.
        const auto& fb = framebuffers_[display_buffer_index()];
        for (std::uint32_t y = 0; y < fb_height; ++y) {
            for (std::uint32_t x = 0; x < fb_width; ++x) {
                const std::size_t off = (static_cast<std::size_t>(y) * fb_width + x) * 2U;
                const std::uint16_t native = static_cast<std::uint16_t>(
                    (std::uint16_t{fb[off]} << 8U) | std::uint16_t{fb[off + 1U]});
                rgb_view_[static_cast<std::size_t>(y) * fb_width + x] = native_to_rgb(native);
            }
        }
        return {.pixels = rgb_view_.data(), .width = fb_width, .height = fb_height, .stride = 0U};
    }

    // ---------------- Register R/W ----------------

    std::uint16_t vdp1::read_register(std::uint8_t offset) const noexcept {
        offset &= 0x1EU;
        switch (offset) {
        case reg_edsr:
            return edsr_;
        case reg_lopr:
            return lopr_;
        case reg_copr:
            return copr_;
        case reg_modr: {
            const std::uint16_t tvmr = regs_[reg_tvmr / 2U] & 0x000FU;
            const std::uint16_t fbcr = regs_[reg_fbcr / 2U] & 0x001EU;
            const std::uint16_t ptmr = regs_[reg_ptmr / 2U] & 0x0002U;
            return static_cast<std::uint16_t>(0x1000U | (ptmr << 7U) | (fbcr << 3U) | tvmr);
        }
        default:
            return regs_[offset / 2U];
        }
    }

    void vdp1::write_register(std::uint8_t offset, std::uint16_t value) noexcept {
        offset &= 0x1EU;
        regs_[offset / 2U] = value;

        switch (offset) {
        case reg_tvmr:
            tvmr_ = value;
            break;
        case reg_fbcr:
            fbcr_ = value;
            break;
        case reg_ptmr:
            if ((value & 0x03U) == ptmr_start) {
                ptmr_ = ptmr_start;
                pending_ptmr_valid_ = false;
                // A manual start rolls the prior draw-end indication into the
                // previous-frame slot, then the new pass clears the current.
                edsr_ = static_cast<std::uint16_t>((edsr_ & 0x0003U) != 0U ? 0x0001U : 0x0000U);
                begin_plot();
            } else {
                ptmr_ = static_cast<std::uint8_t>(value & 0x03U);
                pending_ptmr_ = ptmr_;
                pending_ptmr_valid_ = true;
            }
            break;
        case reg_ewdr:
            ewdr_ = value;
            break;
        case reg_ewlr:
            ewlr_ = value;
            break;
        case reg_ewrr:
            ewrr_ = value;
            break;
        case reg_endr:
            // Writing ENDR strobes draw-end: mark the current frame's end flag.
            state_ = draw_state::done;
            edsr_ = static_cast<std::uint16_t>((edsr_ & 0x0001U) | 0x0002U);
            break;
        default:
            break;
        }
    }

    // ---------------- Erase ----------------

    void vdp1::erase_buffer(std::uint8_t buffer_index) noexcept {
        auto& fb = framebuffers_[buffer_index & 1U];
        const bool mode_8bpp = is_8bpp_framebuffer();
        const int x_scale = mode_8bpp ? 16 : 8;
        const int max_x = mode_8bpp ? 1023 : 511;
        int x1 = static_cast<int>((ewlr_ >> 9U) & 0x3FU) * x_scale;
        int y1 = static_cast<int>(ewlr_ & 0x01FFU);
        const int x2_units = static_cast<int>((ewrr_ >> 9U) & 0x7FU);
        int x2 = (x2_units == 0) ? -1 : (x2_units * x_scale - 1);
        int y2 = static_cast<int>(ewrr_ & 0x01FFU);

        if (x1 < 0) {
            x1 = 0;
        }
        if (y1 < 0) {
            y1 = 0;
        }
        if (x2 > max_x) {
            x2 = max_x;
        }
        if (y2 > static_cast<int>(fb_height) - 1) {
            y2 = static_cast<int>(fb_height) - 1;
        }
        if (x2 < x1 || y2 < y1) {
            return;
        }

        for (int y = y1; y <= y2; ++y) {
            std::uint8_t* row = fb.data() + static_cast<std::size_t>(y) * fb_pitch_bytes;
            if (mode_8bpp) {
                const auto even_fill = static_cast<std::uint8_t>(ewdr_ >> 8U);
                const auto odd_fill = static_cast<std::uint8_t>(ewdr_ & 0x00FFU);
                for (int x = x1; x <= x2; ++x) {
                    row[x] = ((x & 1) == 0) ? even_fill : odd_fill;
                }
            } else {
                const auto hi = static_cast<std::uint8_t>(ewdr_ >> 8U);
                const auto lo = static_cast<std::uint8_t>(ewdr_ & 0x00FFU);
                for (int x = x1; x <= x2; ++x) {
                    const std::size_t off = static_cast<std::size_t>(x) * 2U;
                    row[off] = hi;
                    row[off + 1U] = lo;
                }
            }
        }
    }

    void vdp1::erase_draw_buffer() noexcept { erase_buffer(draw_buffer_index_); }

    void vdp1::erase_display_buffer() noexcept {
        erase_buffer(static_cast<std::uint8_t>(draw_buffer_index_ ^ 1U));
    }

    // ---------------- Drawing lifecycle ----------------

    void vdp1::begin_plot() noexcept {
        erase_draw_buffer();
        cmd_ip_ = 0U;
        copr_ = 0U;
        call_depth_ = 0;
        local_x_ = local_y_ = 0;
        state_ = draw_state::plotting;
        edsr_ &= 0x0001U; // drawing start clears the current-frame end flag
    }

    void vdp1::on_frame_change() noexcept {
        lopr_ = copr_;
        edsr_ = (edsr_ & 0x0002U) != 0U ? 0x0001U : 0x0000U;
        if (pending_ptmr_valid_) {
            ptmr_ = pending_ptmr_;
            pending_ptmr_valid_ = false;
        }
        if (ptmr_ == ptmr_auto) {
            begin_plot();
        } else if (ptmr_ == ptmr_idle) {
            state_ = draw_state::idle;
        }
        ++frame_index_;
    }

    void vdp1::advance_ip(std::uint16_t ctrl) noexcept {
        const std::uint16_t jump_mode = ctrl & ctrl_jump_mask;
        const std::uint16_t link = read_vram_word(cmd_ip_ + 2U);
        const std::uint32_t next_linear =
            (cmd_ip_ + cmd_table_bytes) & static_cast<std::uint32_t>(vram_size - 1U);
        const std::uint32_t next_link =
            (static_cast<std::uint32_t>(link) * 8U) & static_cast<std::uint32_t>(vram_size - 1U);

        switch (jump_mode) {
        case jump_assign:
            cmd_ip_ = next_link;
            break;
        case jump_call:
            if (call_depth_ < static_cast<int>(call_stack_.size())) {
                call_stack_[static_cast<std::size_t>(call_depth_++)] = next_linear;
            }
            cmd_ip_ = next_link;
            break;
        case jump_return:
            if (call_depth_ > 0) {
                cmd_ip_ = call_stack_[static_cast<std::size_t>(--call_depth_)];
            } else {
                cmd_ip_ = next_linear;
            }
            break;
        case jump_next:
        default:
            cmd_ip_ = next_linear;
            break;
        }
    }

    bool vdp1::step_command() noexcept {
        if (state_ != draw_state::plotting) {
            return false;
        }

        const std::uint32_t cmd_base = cmd_ip_;
        const std::uint16_t ctrl = read_vram_word(cmd_base + 0x00U);
        copr_ = static_cast<std::uint16_t>(cmd_base >> 3U);

        if ((ctrl & ctrl_end) != 0U) {
            state_ = draw_state::done;
            edsr_ = static_cast<std::uint16_t>((edsr_ & 0x0001U) | 0x0002U);
            return true;
        }
        if ((ctrl & ctrl_skip) != 0U) {
            advance_ip(ctrl);
            return true;
        }

        const std::uint16_t pmod = read_vram_word(cmd_base + 0x04U);
        const std::uint16_t colr = read_vram_word(cmd_base + 0x06U);
        const auto cmd_type = static_cast<std::uint8_t>(ctrl & 0x000FU);

        switch (cmd_type) {
        case cmd_normal_sprite:
            draw_normal_sprite(cmd_base, pmod, colr);
            break;
        case cmd_polygon:
            draw_polygon(cmd_base, pmod, colr);
            break;
        case cmd_user_clip:
            exec_user_clip(cmd_base);
            break;
        case cmd_system_clip:
            exec_system_clip(cmd_base);
            break;
        case cmd_local_coord:
            exec_local_coord(cmd_base);
            break;
        // Scaled/distorted sprite, polyline, line: parsed but not yet drawn.
        default:
            break;
        }

        advance_ip(ctrl);
        return true;
    }

    int vdp1::run_to_end(int max_commands) noexcept {
        int n = 0;
        while (n < max_commands && step_command()) {
            ++n;
            if (state_ != draw_state::plotting) {
                break;
            }
        }
        return n;
    }

    // ---------------- Clipping ----------------

    bool vdp1::pixel_passes_clip(std::int32_t x, std::int32_t y,
                                 std::uint16_t pmod) const noexcept {
        if (x < sys_clip_x1_ || x > sys_clip_x2_) {
            return false;
        }
        if (y < sys_clip_y1_ || y > sys_clip_y2_) {
            return false;
        }
        if ((pmod & 0x0400U) != 0U) { // user-clip enable
            const bool inside = x >= user_clip_x1_ && x <= user_clip_x2_ && y >= user_clip_y1_ &&
                                y <= user_clip_y2_;
            const bool outside_mode = (pmod & 0x0200U) != 0U;
            if (outside_mode ? inside : !inside) {
                return false;
            }
        }
        return true;
    }

    void vdp1::exec_local_coord(std::uint32_t cmd_base) noexcept {
        local_x_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x0CU));
        local_y_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x0EU));
    }

    void vdp1::exec_system_clip(std::uint32_t cmd_base) noexcept {
        sys_clip_x1_ = 0;
        sys_clip_y1_ = 0;
        sys_clip_x2_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x14U));
        sys_clip_y2_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x16U));
        if (sys_clip_x2_ < 0) {
            sys_clip_x2_ = 0;
        }
        if (sys_clip_y2_ < 0) {
            sys_clip_y2_ = 0;
        }
        const auto width = static_cast<std::int16_t>(framebuffer_width_pixels() - 1U);
        if (sys_clip_x2_ > width) {
            sys_clip_x2_ = width;
        }
        if (sys_clip_y2_ > static_cast<std::int16_t>(fb_height - 1U)) {
            sys_clip_y2_ = static_cast<std::int16_t>(fb_height - 1U);
        }
    }

    void vdp1::exec_user_clip(std::uint32_t cmd_base) noexcept {
        user_clip_x1_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x0CU));
        user_clip_y1_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x0EU));
        user_clip_x2_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x14U));
        user_clip_y2_ = static_cast<std::int16_t>(read_vram_word(cmd_base + 0x16U));
    }

    // ---------------- Texture sample ----------------

    vdp1::sample vdp1::fetch_texel(std::uint32_t src_addr, std::uint16_t color_bank,
                                   std::uint8_t color_mode, std::uint16_t pmod, std::int32_t tx,
                                   std::int32_t ty, std::int32_t tw) const noexcept {
        const std::uint32_t pix_index =
            static_cast<std::uint32_t>(ty) * static_cast<std::uint32_t>(tw) +
            static_cast<std::uint32_t>(tx);
        const bool spd = (pmod & 0x0040U) != 0U; // suppress-transparent (draw pen 0)
        std::uint16_t native = 0U;

        switch (color_mode) {
        case 0: // 4bpp colour bank
        case 1: // 4bpp colour LUT
        {
            const std::uint32_t byte_off = src_addr + (pix_index >> 1U);
            const std::uint8_t byte = vram_[byte_off & static_cast<std::uint32_t>(vram_size - 1U)];
            const std::uint8_t nibble =
                (pix_index & 1U) != 0U ? (byte & 0x0FU) : static_cast<std::uint8_t>(byte >> 4U);
            if (nibble == 0U && !spd) {
                return {.color = 0U, .transparent = true};
            }
            if (color_mode == 0) {
                const auto raw = static_cast<std::uint16_t>((color_bank & 0xFFF0U) | nibble);
                const auto idx = static_cast<std::uint16_t>(raw & 0x7FFU);
                if (cram_reader_) {
                    native = cram_reader_(idx);
                } else {
                    const auto ramp = static_cast<std::uint16_t>((color_bank + nibble) & 0x1FU);
                    native =
                        static_cast<std::uint16_t>(0x8000U | (ramp << 10U) | (ramp << 5U) | ramp);
                }
            } else {
                const std::uint32_t lut_off = static_cast<std::uint32_t>(color_bank) * 8U +
                                              static_cast<std::uint32_t>(nibble) * 2U;
                native = read_vram_word(lut_off);
            }
            break;
        }
        case 2: // 8bpp 64-colour bank
        case 3: // 8bpp 128-colour bank
        case 4: // 8bpp 256-colour bank
        {
            const std::uint32_t byte_off = src_addr + pix_index;
            const std::uint8_t dot = vram_[byte_off & static_cast<std::uint32_t>(vram_size - 1U)];
            std::uint16_t dot_mask = 0x00FFU;
            std::uint16_t bank_mask = 0xFF00U;
            if (color_mode == 2U) {
                dot_mask = 0x003FU;
                bank_mask = 0xFFC0U;
            } else if (color_mode == 3U) {
                dot_mask = 0x007FU;
                bank_mask = 0xFF80U;
            }
            if ((dot & dot_mask) == 0U && !spd) {
                return {.color = 0U, .transparent = true};
            }
            const auto raw =
                static_cast<std::uint16_t>((color_bank & bank_mask) | (dot & dot_mask));
            const auto idx = static_cast<std::uint16_t>(raw & 0x7FFU);
            if (cram_reader_) {
                native = cram_reader_(idx);
            } else {
                const auto r = static_cast<std::uint16_t>(dot & 0x1FU);
                const auto g = static_cast<std::uint16_t>((dot >> 3U) & 0x1FU);
                native = static_cast<std::uint16_t>(0x8000U | (r << 10U) | (g << 5U) | r);
            }
            break;
        }
        case 5: // 16bpp direct RGB
        default: {
            const std::uint32_t word_off = src_addr + pix_index * 2U;
            native = read_vram_word(word_off);
            if (native == 0x0000U && !spd) {
                return {.color = 0U, .transparent = true};
            }
            break;
        }
        }

        if ((pmod & 0x8000U) != 0U) {
            native |= 0x8000U;
        }
        return {.color = native, .transparent = false};
    }

    // ---------------- Primitives ----------------

    void vdp1::draw_normal_sprite(std::uint32_t cmd_base, std::uint16_t pmod,
                                  std::uint16_t colr) noexcept {
        const std::uint16_t srca = read_vram_word(cmd_base + 0x08U);
        const std::uint16_t size = read_vram_word(cmd_base + 0x0AU);
        const std::int32_t xa =
            static_cast<std::int16_t>(read_vram_word(cmd_base + 0x0CU)) + local_x_;
        const std::int32_t ya =
            static_cast<std::int16_t>(read_vram_word(cmd_base + 0x0EU)) + local_y_;
        const std::int32_t sw = static_cast<std::int32_t>((size >> 8U) & 0x3FU) * 8;
        const std::int32_t sh = static_cast<std::int32_t>(size & 0xFFU);
        if (sw <= 0 || sh <= 0) {
            return;
        }
        const std::uint32_t src_addr = static_cast<std::uint32_t>(srca) * 8U;
        const auto color_mode = static_cast<std::uint8_t>((pmod >> 3U) & 0x7U);
        const auto dir = static_cast<std::uint8_t>((read_vram_word(cmd_base) >> 4U) & 0x3U);
        const bool flip_x = (dir & 0x1U) != 0U;
        const bool flip_y = (dir & 0x2U) != 0U;

        for (std::int32_t py = 0; py < sh; ++py) {
            const std::int32_t y = ya + py;
            const std::int32_t ty = flip_y ? (sh - 1 - py) : py;
            for (std::int32_t px = 0; px < sw; ++px) {
                const std::int32_t x = xa + px;
                if (!pixel_passes_clip(x, y, pmod)) {
                    continue;
                }
                const std::int32_t tx = flip_x ? (sw - 1 - px) : px;
                const sample s = fetch_texel(src_addr, colr, color_mode, pmod, tx, ty, sw);
                if (s.transparent) {
                    continue;
                }
                fb_write(x, y, s.color); // replace pixel-op only
            }
        }
    }

    void vdp1::draw_polygon(std::uint32_t cmd_base, std::uint16_t pmod,
                            std::uint16_t colr) noexcept {
        std::array<std::int32_t, 4> xs{};
        std::array<std::int32_t, 4> ys{};
        for (std::size_t i = 0; i < 4U; ++i) {
            xs[i] = static_cast<std::int16_t>(
                        read_vram_word(cmd_base + 0x0CU + static_cast<std::uint32_t>(i) * 4U)) +
                    local_x_;
            ys[i] = static_cast<std::int16_t>(
                        read_vram_word(cmd_base + 0x0EU + static_cast<std::uint32_t>(i) * 4U)) +
                    local_y_;
        }
        std::int32_t min_x = xs[0];
        std::int32_t max_x = xs[0];
        std::int32_t min_y = ys[0];
        std::int32_t max_y = ys[0];
        for (std::size_t i = 1; i < 4U; ++i) {
            min_x = std::min(min_x, xs[i]);
            max_x = std::max(max_x, xs[i]);
            min_y = std::min(min_y, ys[i]);
            max_y = std::max(max_y, ys[i]);
        }
        min_x = std::max<std::int32_t>(min_x, sys_clip_x1_);
        max_x = std::min<std::int32_t>(max_x, sys_clip_x2_);
        min_y = std::max<std::int32_t>(min_y, sys_clip_y1_);
        max_y = std::min<std::int32_t>(max_y, sys_clip_y2_);

        // Solid quad fill: point-in-quad via edge cross-products, either
        // winding. colr is a native 16bpp colour for polygon fills.
        const auto inside_quad = [&](std::int32_t x, std::int32_t y, bool ccw) {
            for (std::size_t i = 0; i < 4U; ++i) {
                const std::size_t j = (i + 1U) & 3U;
                const std::int32_t ex = xs[j] - xs[i];
                const std::int32_t ey = ys[j] - ys[i];
                const std::int32_t cross = ex * (y - ys[i]) - ey * (x - xs[i]);
                if (ccw ? (cross < 0) : (cross > 0)) {
                    return false;
                }
            }
            return true;
        };

        for (std::int32_t y = min_y; y <= max_y; ++y) {
            for (std::int32_t x = min_x; x <= max_x; ++x) {
                if (!inside_quad(x, y, true) && !inside_quad(x, y, false)) {
                    continue;
                }
                if (!pixel_passes_clip(x, y, pmod)) {
                    continue;
                }
                fb_write(x, y, colr);
            }
        }
    }

    // ---------------- State ----------------

    void vdp1::save_state(state_writer& writer) const {
        writer.bytes(vram_);
        writer.bytes(framebuffers_[0]);
        writer.bytes(framebuffers_[1]);
        writer.u8(draw_buffer_index_);
        for (const std::uint16_t r : regs_) {
            writer.u16(r);
        }
        writer.u16(tvmr_);
        writer.u16(fbcr_);
        writer.u8(ptmr_);
        writer.u16(ewdr_);
        writer.u16(ewlr_);
        writer.u16(ewrr_);
        writer.u16(edsr_);
        writer.u16(lopr_);
        writer.u16(copr_);
        writer.u8(pending_ptmr_);
        writer.boolean(pending_ptmr_valid_);
        writer.u8(static_cast<std::uint8_t>(state_));
        writer.u32(cmd_ip_);
        for (const std::uint32_t s : call_stack_) {
            writer.u32(s);
        }
        writer.u32(static_cast<std::uint32_t>(call_depth_));
        writer.u16(static_cast<std::uint16_t>(local_x_));
        writer.u16(static_cast<std::uint16_t>(local_y_));
        writer.u16(static_cast<std::uint16_t>(sys_clip_x1_));
        writer.u16(static_cast<std::uint16_t>(sys_clip_y1_));
        writer.u16(static_cast<std::uint16_t>(sys_clip_x2_));
        writer.u16(static_cast<std::uint16_t>(sys_clip_y2_));
        writer.u16(static_cast<std::uint16_t>(user_clip_x1_));
        writer.u16(static_cast<std::uint16_t>(user_clip_y1_));
        writer.u16(static_cast<std::uint16_t>(user_clip_x2_));
        writer.u16(static_cast<std::uint16_t>(user_clip_y2_));
        writer.u64(frame_index_);
    }

    void vdp1::load_state(state_reader& reader) {
        reader.bytes(vram_);
        reader.bytes(framebuffers_[0]);
        reader.bytes(framebuffers_[1]);
        draw_buffer_index_ = reader.u8();
        for (std::uint16_t& r : regs_) {
            r = reader.u16();
        }
        tvmr_ = reader.u16();
        fbcr_ = reader.u16();
        ptmr_ = reader.u8();
        ewdr_ = reader.u16();
        ewlr_ = reader.u16();
        ewrr_ = reader.u16();
        edsr_ = reader.u16();
        lopr_ = reader.u16();
        copr_ = reader.u16();
        pending_ptmr_ = reader.u8();
        pending_ptmr_valid_ = reader.boolean();
        state_ = static_cast<draw_state>(reader.u8());
        cmd_ip_ = reader.u32();
        for (std::uint32_t& s : call_stack_) {
            s = reader.u32();
        }
        call_depth_ = static_cast<int>(reader.u32());
        local_x_ = static_cast<std::int16_t>(reader.u16());
        local_y_ = static_cast<std::int16_t>(reader.u16());
        sys_clip_x1_ = static_cast<std::int16_t>(reader.u16());
        sys_clip_y1_ = static_cast<std::int16_t>(reader.u16());
        sys_clip_x2_ = static_cast<std::int16_t>(reader.u16());
        sys_clip_y2_ = static_cast<std::int16_t>(reader.u16());
        user_clip_x1_ = static_cast<std::int16_t>(reader.u16());
        user_clip_y1_ = static_cast<std::int16_t>(reader.u16());
        user_clip_x2_ = static_cast<std::int16_t>(reader.u16());
        user_clip_y2_ = static_cast<std::int16_t>(reader.u16());
        frame_index_ = reader.u64();
    }

    instrumentation::ichip_introspection& vdp1::introspection() noexcept { return introspection_; }

    namespace {
        [[maybe_unused]] const auto vdp1_registration =
            register_factory("sega.vdp1", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<vdp1>(); });
    } // namespace

} // namespace mnemos::chips::video
