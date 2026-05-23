#include <mnemos/chips/video/vic_ii_6569.hpp>

#include <mnemos/chips/common/chip_registry.hpp>

#include <memory>

namespace mnemos::chips::video {
    namespace {

        // Register indices within the $D000 window.
        constexpr std::uint8_t reg_sp0x = 0x00U;
        constexpr std::uint8_t reg_sp0y = 0x01U;
        constexpr std::uint8_t reg_msigx = 0x10U;
        constexpr std::uint8_t reg_scroly = 0x11U;
        constexpr std::uint8_t reg_raster = 0x12U;
        constexpr std::uint8_t reg_lpx = 0x13U;
        constexpr std::uint8_t reg_lpy = 0x14U;
        constexpr std::uint8_t reg_scrolx = 0x16U;
        constexpr std::uint8_t reg_vicirq = 0x19U;
        constexpr std::uint8_t reg_irqmsk = 0x1AU;
        constexpr std::uint8_t reg_sscol = 0x1EU;
        constexpr std::uint8_t reg_sbcol = 0x1FU;

        constexpr std::uint8_t irq_raster = 0x01U;
        constexpr std::uint8_t irq_light_pen = 0x08U;
        constexpr std::uint8_t irq_sources = 0x0FU;
        constexpr std::uint8_t irq_master = 0x80U;

        constexpr std::uint8_t scroly_rst8 = 0x80U;
        constexpr std::uint8_t scroly_ecm = 0x40U;
        constexpr std::uint8_t scroly_bmm = 0x20U;
        constexpr std::uint8_t scroly_den = 0x10U;
        constexpr std::uint8_t scroly_rsel = 0x08U;
        constexpr std::uint8_t scroly_yscroll = 0x07U;

        constexpr std::uint8_t scrolx_res = 0x20U;
        constexpr std::uint8_t scrolx_mcm = 0x10U;
        constexpr std::uint8_t scrolx_csel = 0x08U;
        constexpr std::uint8_t scrolx_xscroll = 0x07U;

        // Pepto VIC-II palette (real-hardware calibration), 0x00RRGGBB.
        constexpr std::array<std::uint32_t, 16> k_palette = {
            0x00000000U, 0x00FFFFFFU, 0x0068372BU, 0x0070A4B2U, 0x006F3D86U, 0x00588D43U,
            0x00352879U, 0x00B8C76FU, 0x006F4F25U, 0x00433900U, 0x009A6759U, 0x00444444U,
            0x006C6C6CU, 0x009AD284U, 0x006C5EB5U, 0x00959595U,
        };

    } // namespace

    chip_metadata vic_ii_6569::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "6569",
            .family = "VIC-II",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void vic_ii_6569::reset(reset_kind /*kind*/) {
        // is_pal_/rev_ are machine configuration and survive reset.
        regs_.fill(0U);
        sprite_x_.fill(0U);
        sprite_y_.fill(0U);
        raster_compare_ = 0U;
        raster_y_ = 0U;
        raster_x_ = 0U;
        modes_ = mode_flags{};
        den_latched_line_30_ = false;
        vc_ = 0U;
        vcbase_ = 0U;
        rc_ = 0U;
        vmli_ = 0U;
        display_state_ = false;
        raster_match_active_ = false;
        update_irq_registers();
    }

    void vic_ii_6569::set_revision(revision rev) noexcept {
        rev_ = rev;
        // Keep the video standard coherent with the chosen part.
        is_pal_ = (rev == revision::pal_6569 || rev == revision::pal_8565);
    }

    std::uint8_t vic_ii_6569::current_irq_sources() const noexcept {
        return static_cast<std::uint8_t>(regs_[reg_vicirq] & irq_sources);
    }

    void vic_ii_6569::decode_modes() noexcept {
        const std::uint8_t sy = regs_[reg_scroly];
        const std::uint8_t sx = regs_[reg_scrolx];
        modes_.ecm = (sy & scroly_ecm) != 0U;
        modes_.bmm = (sy & scroly_bmm) != 0U;
        modes_.den = (sy & scroly_den) != 0U;
        modes_.rsel = (sy & scroly_rsel) != 0U;
        modes_.yscroll = static_cast<std::uint8_t>(sy & scroly_yscroll);
        modes_.res = (sx & scrolx_res) != 0U;
        modes_.mcm = (sx & scrolx_mcm) != 0U;
        modes_.csel = (sx & scrolx_csel) != 0U;
        modes_.xscroll = static_cast<std::uint8_t>(sx & scrolx_xscroll);
    }

    void vic_ii_6569::refresh_sprite_x() noexcept {
        const std::uint8_t msb = regs_[reg_msigx];
        for (std::uint8_t i = 0; i < sprite_count; ++i) {
            const auto low = static_cast<std::uint16_t>(regs_[reg_sp0x + i * 2U]);
            const auto hi = static_cast<std::uint16_t>((msb & (1U << i)) != 0U ? 0x0100U : 0x0000U);
            sprite_x_[i] = static_cast<std::uint16_t>(low | hi);
        }
    }

    void vic_ii_6569::refresh_sprite_y() noexcept {
        for (std::uint8_t i = 0; i < sprite_count; ++i) {
            sprite_y_[i] = regs_[reg_sp0y + i * 2U];
        }
    }

    void vic_ii_6569::refresh_raster_compare() noexcept {
        const auto lo = static_cast<std::uint16_t>(regs_[reg_raster]);
        const auto hi =
            static_cast<std::uint16_t>((regs_[reg_scroly] & scroly_rst8) != 0U ? 0x0100U : 0x0000U);
        raster_compare_ = static_cast<std::uint16_t>(lo | hi);
    }

    void vic_ii_6569::update_irq_registers() noexcept {
        const std::uint8_t sources = current_irq_sources();
        const auto mask = static_cast<std::uint8_t>(regs_[reg_irqmsk] & irq_sources);
        const std::uint8_t master = ((sources & mask) != 0U) ? irq_master : 0U;
        regs_[reg_vicirq] = static_cast<std::uint8_t>(sources | master);
        regs_[reg_irqmsk] = mask;
    }

    void vic_ii_6569::latch_irq_source(std::uint8_t source) noexcept {
        regs_[reg_vicirq] =
            static_cast<std::uint8_t>((current_irq_sources() | source) & irq_sources);
        update_irq_registers();
    }

    void vic_ii_6569::refresh_raster_irq_edge() noexcept {
        const bool match = (raster_y_ == raster_compare_);
        if (match && !raster_match_active_) {
            latch_irq_source(irq_raster);
        }
        raster_match_active_ = match;
    }

    void vic_ii_6569::update_den_latch() noexcept {
        if (raster_y_ == 0x30U && modes_.den) {
            den_latched_line_30_ = true;
        }
    }

    void vic_ii_6569::advance_video_counters(std::uint16_t cycle) noexcept {
        // Bauer §3.7.2. Cycle 14 (idx 13): reload VC from VCBASE, clear VMLI; a
        // Bad Line arms display state and zeroes RC.
        if (cycle == 13U) {
            vc_ = vcbase_;
            vmli_ = 0U;
            if (bad_line_condition()) {
                display_state_ = true;
                rc_ = 0U;
            }
        }
        // Cycles 15..54 (idx 14..53): g-accesses increment VC + VMLI in display.
        if (display_state_ && cycle >= 14U && cycle <= 53U) {
            vc_ = static_cast<std::uint16_t>((vc_ + 1U) & 0x03FFU);
            vmli_ = static_cast<std::uint8_t>((vmli_ + 1U) & 0x3FU);
        }
        // Cycle 58 (idx 57): if RC wrapped, latch VCBASE and go idle; else RC++.
        if (cycle == 57U) {
            if (rc_ == 7U) {
                display_state_ = false;
                vcbase_ = static_cast<std::uint16_t>(vc_ & 0x03FFU);
            }
            if (display_state_) {
                rc_ = static_cast<std::uint8_t>((rc_ + 1U) & 0x07U);
            }
        }
    }

    std::uint8_t vic_ii_6569::read(std::uint8_t address) noexcept {
        const auto reg = static_cast<std::uint8_t>(address & 0x3FU);

        // $D02F..$D03F decode to all-ones through the bus pull-ups.
        if (reg >= 0x2FU) {
            return 0xFFU;
        }

        switch (reg) {
        case reg_raster:
            return static_cast<std::uint8_t>(raster_y_ & 0x00FFU);
        case reg_scroly: {
            auto value = static_cast<std::uint8_t>(regs_[reg_scroly] & 0x7FU);
            if ((raster_y_ & 0x0100U) != 0U) {
                value = static_cast<std::uint8_t>(value | scroly_rst8);
            }
            return value;
        }
        case reg_vicirq:
            return static_cast<std::uint8_t>(0x70U |
                                             (regs_[reg_vicirq] & (irq_master | irq_sources)));
        case reg_irqmsk:
            return static_cast<std::uint8_t>(0xF0U | (regs_[reg_irqmsk] & irq_sources));
        case reg_sscol: {
            const std::uint8_t value = regs_[reg_sscol]; // read-to-clear latch
            regs_[reg_sscol] = 0U;
            return value;
        }
        case reg_sbcol: {
            const std::uint8_t value = regs_[reg_sbcol]; // read-to-clear latch
            regs_[reg_sbcol] = 0U;
            return value;
        }
        default:
            return regs_[reg];
        }
    }

    void vic_ii_6569::write(std::uint8_t address, std::uint8_t value) noexcept {
        const auto reg = static_cast<std::uint8_t>(address & 0x3FU);
        if (reg >= 0x2FU) {
            return; // writes to the unused/aliased range are dropped
        }

        // VICIRQ ($D019): writing 1 to a source bit acknowledges that latch.
        if (reg == reg_vicirq) {
            const auto cleared =
                static_cast<std::uint8_t>(current_irq_sources() & ~(value & irq_sources));
            regs_[reg_vicirq] = static_cast<std::uint8_t>(
                (regs_[reg_vicirq] & static_cast<std::uint8_t>(~irq_sources)) | cleared);
            update_irq_registers();
            return;
        }

        // Collision latches ($D01E/$D01F) only clear on read; writes are ignored.
        if (reg == reg_sscol || reg == reg_sbcol) {
            return;
        }

        regs_[reg] = value;

        switch (reg) {
        case reg_scroly:
            decode_modes();
            refresh_raster_compare();
            update_den_latch();
            refresh_raster_irq_edge();
            break;
        case reg_scrolx:
            decode_modes();
            break;
        case reg_raster:
            refresh_raster_compare();
            refresh_raster_irq_edge();
            break;
        case reg_irqmsk:
            regs_[reg_irqmsk] = static_cast<std::uint8_t>(value & irq_sources);
            update_irq_registers();
            break;
        default:
            if (reg == reg_msigx || (reg < 0x10U && (reg & 1U) == 0U)) {
                refresh_sprite_x(); // MSIGX or a sprite X low byte
            } else if (reg < 0x10U) {
                refresh_sprite_y(); // a sprite Y byte
            }
            break;
        }
    }

    void vic_ii_6569::tick(std::uint64_t cycles) {
        const std::uint16_t cpl = cycles_per_line();
        const std::uint16_t tl = total_lines();
        for (std::uint64_t i = 0; i < cycles; ++i) {
            update_den_latch();
            raster_x_ = static_cast<std::uint16_t>(raster_x_ + 1U);
            if (raster_x_ >= cpl) {
                raster_x_ = 0U;
                raster_y_ = static_cast<std::uint16_t>(raster_y_ + 1U);
                if (raster_y_ >= tl) {
                    raster_y_ = 0U;
                    den_latched_line_30_ = false;
                }
                update_den_latch();
                refresh_raster_irq_edge();
                if (raster_y_ == 0U) {
                    vcbase_ = 0U; // VCBASE clears once per frame at line 0
                }
            }
            advance_video_counters(raster_x_);
        }
    }

    void vic_ii_6569::set_raster(std::uint16_t line) noexcept {
        raster_y_ = static_cast<std::uint16_t>(line % total_lines());
        raster_x_ = 0U;
        if (raster_y_ == 0U) {
            den_latched_line_30_ = false;
        }
        update_den_latch();
        refresh_raster_irq_edge();
    }

    void vic_ii_6569::trigger_light_pen(std::uint16_t x, std::uint16_t y) noexcept {
        regs_[reg_lpx] = static_cast<std::uint8_t>((x >> 1U) & 0xFFU);
        regs_[reg_lpy] = static_cast<std::uint8_t>(y & 0xFFU);
        latch_irq_source(irq_light_pen);
    }

    std::uint16_t vic_ii_6569::total_lines() const noexcept {
        if (is_pal_) {
            return 312U;
        }
        return rev_ == revision::ntsc_6567r56a ? 262U : 263U;
    }

    std::uint16_t vic_ii_6569::cycles_per_line() const noexcept {
        if (is_pal_) {
            return 63U;
        }
        return rev_ == revision::ntsc_6567r56a ? 64U : 65U;
    }

    bool vic_ii_6569::irq_asserted() const noexcept {
        return (regs_[reg_vicirq] & irq_master) != 0U;
    }

    bool vic_ii_6569::bad_line_condition() const noexcept {
        return den_latched_line_30_ && raster_y_ >= 0x30U && raster_y_ <= 0xF7U &&
               static_cast<std::uint16_t>(raster_y_ & 0x07U) == modes_.yscroll;
    }

    bool vic_ii_6569::ba_low() const noexcept {
        return bad_line_condition() && raster_x_ >= 12U && raster_x_ <= 54U;
    }

    bool vic_ii_6569::cpu_read_stalled() const noexcept { return ba_low() && raster_x_ >= 15U; }

    std::uint32_t vic_ii_6569::color_rgb888(std::uint8_t color_index) noexcept {
        return k_palette[color_index & 0x0FU];
    }

    void vic_ii_6569::save_state(state_writer& /*writer*/) const {
        // Serialization lands with the M3 runtime save-state format.
    }

    void vic_ii_6569::load_state(state_reader& /*reader*/) {
        // Deserialization lands with the M3 runtime save-state format.
    }

    instrumentation::i_chip_introspection& vic_ii_6569::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> vic_ii_6569::register_snapshot() noexcept {
        register_view_[0] = {"RASTER_Y", raster_y_, 9U, register_value_format::unsigned_integer};
        register_view_[1] = {"RASTER_X", raster_x_, 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"RASTER_CMP", raster_compare_, 9U,
                             register_value_format::unsigned_integer};
        register_view_[3] = {"VICIRQ", regs_[reg_vicirq], 8U, register_value_format::flags};
        register_view_[4] = {"SCROLY", regs_[reg_scroly], 8U, register_value_format::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto vic_ii_6569_registration =
            register_factory("mos.6569", chip_class::video, []() -> std::unique_ptr<i_chip> {
                return std::make_unique<vic_ii_6569>();
            });
    } // namespace

} // namespace mnemos::chips::video
