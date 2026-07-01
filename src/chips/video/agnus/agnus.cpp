#include "agnus.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // 4-bit colour channel to 8 bits, the nibble replicated into the low
        // bits so 0xF -> 0xFF and 0x0 -> 0x00 (the hardware DAC rule, ported
        // integer-exact from the reference colour decode).
        [[nodiscard]] constexpr std::uint32_t expand4(std::uint8_t nibble) noexcept {
            const std::uint32_t v = nibble & 0x0FU;
            return (v << 4U) | v;
        }

        // Custom-register offsets the copper can MOVE into to drive the
        // display. Mirrors the standard OCS register map; only the registers
        // this display sequencer models are honoured (others are dropped).
        constexpr std::uint16_t reg_dmacon = 0x096U;
        constexpr std::uint16_t reg_bplcon0 = 0x100U;
        constexpr std::uint16_t bplcon0_lace = 0x0004U;
        constexpr std::uint16_t reg_bplcon1 = 0x102U;
        constexpr std::uint16_t reg_bplcon2 = 0x104U;
        constexpr std::uint16_t reg_bpl1mod = 0x108U;
        constexpr std::uint16_t reg_bpl2mod = 0x10AU;
        constexpr std::uint16_t reg_diwstrt = 0x08EU;
        constexpr std::uint16_t reg_diwstop = 0x090U;
        constexpr std::uint16_t reg_ddfstrt = 0x092U;
        constexpr std::uint16_t reg_ddfstop = 0x094U;
        constexpr std::uint16_t reg_clxcon = 0x098U;
        constexpr std::uint16_t reg_bplpth_base = 0x0E0U; // BPL1PTH; pairs step by 4
        constexpr std::uint16_t reg_sprpth_base = 0x120U; // SPR0PTH; pairs step by 4
        constexpr std::uint16_t reg_sprpos_base = 0x140U; // SPR0POS; groups step by 8
        constexpr std::uint16_t reg_color_base = 0x180U;  // COLOR00; entries step by 2
        constexpr std::uint8_t pf1_sample = 0x01U;
        constexpr std::uint8_t pf2_sample = 0x02U;

        // Copper WAIT/SKIP: IR2 bit 15 is Blitter-Finished-Disable.
        constexpr std::uint16_t copper_bfd_mask = 0x8000U;
        constexpr std::uint8_t copper_move_skip_cycles = 4U;
        constexpr std::uint8_t copper_wait_wake_cycles = 6U;
        constexpr std::uint32_t cpu_cycles_per_memory_slot = 2U;
        constexpr std::uint32_t sprite_dma_first_slot = 0x18U;
        constexpr std::uint32_t sprite_dma_slots_per_channel = 2U;
        constexpr std::uint32_t sprite_dma_slot_count =
            agnus::max_sprites * sprite_dma_slots_per_channel;
        constexpr std::uint16_t copper_never_writable_register_limit = 0x0040U;
        constexpr std::uint16_t copper_danger_register_limit = 0x0080U;
        constexpr std::uint16_t sprite_attach = 0x0080U;
        constexpr std::uint32_t max_sprite_dma_blocks = agnus::scanlines_pal + 1U;
        constexpr std::uint32_t chip_address_mask = 0x001FFFFEU;
        constexpr std::uint32_t chip_address_high_mask = 0x001FU;

        struct copper_trace_config final {
            bool enabled{};
            std::uint32_t first{};
            std::uint32_t last{};
        };

        [[nodiscard]] std::uint32_t parse_trace_hex(const char* text,
                                                    const char** end) noexcept {
            std::uint32_t value = 0U;
            const char* cursor = text;
            while (*cursor == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
                cursor += 2;
            }
            while (*cursor != '\0') {
                std::uint32_t digit = 0U;
                if (*cursor >= '0' && *cursor <= '9') {
                    digit = static_cast<std::uint32_t>(*cursor - '0');
                } else if (*cursor >= 'a' && *cursor <= 'f') {
                    digit = static_cast<std::uint32_t>(10 + *cursor - 'a');
                } else if (*cursor >= 'A' && *cursor <= 'F') {
                    digit = static_cast<std::uint32_t>(10 + *cursor - 'A');
                } else {
                    break;
                }
                value = static_cast<std::uint32_t>((value << 4U) | digit);
                ++cursor;
            }
            *end = cursor;
            return value;
        }

        [[nodiscard]] copper_trace_config copper_trace_from_env() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* env = std::getenv("MNEMOS_AGNUS_COPPER_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (env == nullptr || env[0] == '\0' || (env[0] == '0' && env[1] == '\0')) {
                return {};
            }
            if (env[0] == '1' && env[1] == '\0') {
                return {.enabled = true, .first = 0U, .last = chip_address_mask};
            }

            const char* end = env;
            const std::uint32_t first = parse_trace_hex(env, &end) & chip_address_mask;
            if (*end != '-') {
                return {.enabled = true, .first = first, .last = first};
            }
            const std::uint32_t last = parse_trace_hex(end + 1, &end) & chip_address_mask;
            return {.enabled = true, .first = first, .last = last};
        }

        [[nodiscard]] bool copper_trace_matches(std::uint32_t pc,
                                                std::uint32_t address_mask) noexcept {
            static const copper_trace_config config = copper_trace_from_env();
            if (!config.enabled) {
                return false;
            }
            const std::uint32_t clipped_pc = pc & address_mask;
            if (config.first <= config.last) {
                return clipped_pc >= config.first && clipped_pc <= config.last;
            }
            return clipped_pc >= config.first || clipped_pc <= config.last;
        }

        [[nodiscard]] constexpr std::uint32_t vblank_end_line(bool pal) noexcept {
            return pal ? 24U : 20U;
        }

        [[nodiscard]] std::size_t mirrored_chip_word_address(std::span<const std::uint8_t> ram,
                                                             std::uint32_t byte_address) noexcept {
            const std::size_t mirrored_bytes = ram.size() & ~std::size_t{1U};
            if (mirrored_bytes == 0U) {
                return 0U;
            }
            return static_cast<std::size_t>(byte_address & chip_address_mask) % mirrored_bytes;
        }

        [[nodiscard]] bool copper_can_write_register(std::uint16_t reg_addr,
                                                     bool copper_danger) noexcept {
            if (reg_addr < copper_never_writable_register_limit) {
                return false;
            }
            if (reg_addr < copper_danger_register_limit) {
                return copper_danger;
            }
            return true;
        }

        [[nodiscard]] std::uint32_t sprite_hstart(std::uint16_t pos, std::uint16_t ctl) noexcept {
            return (((static_cast<std::uint32_t>(pos) & 0x00FFU) << 1U) |
                    (static_cast<std::uint32_t>(ctl) & 0x0001U));
        }

        [[nodiscard]] std::uint32_t sprite_vstart(std::uint16_t pos, std::uint16_t ctl) noexcept {
            return (((static_cast<std::uint32_t>(pos) >> 8U) & 0x00FFU) |
                    ((static_cast<std::uint32_t>(ctl) & 0x0004U) << 6U));
        }

        [[nodiscard]] std::uint32_t sprite_vstop(std::uint16_t ctl) noexcept {
            return (((static_cast<std::uint32_t>(ctl) >> 8U) & 0x00FFU) |
                    ((static_cast<std::uint32_t>(ctl) & 0x0002U) << 7U));
        }

        [[nodiscard]] std::int32_t sprite_visible_x(std::uint16_t pos, std::uint16_t ctl) noexcept {
            return static_cast<std::int32_t>(sprite_hstart(pos, ctl)) -
                   static_cast<std::int32_t>(agnus::display_clock_origin);
        }

        [[nodiscard]] std::uint32_t sprite_value(std::uint16_t packed,
                                                 std::uint32_t sprite) noexcept {
            return (static_cast<std::uint32_t>(packed) >> (sprite * 2U)) & 0x03U;
        }

        [[nodiscard]] std::uint8_t sprite_group_collision_bit(std::uint32_t a,
                                                              std::uint32_t b) noexcept {
            constexpr std::array<std::array<std::uint8_t, 4>, 4> bits{{
                {{0U, 9U, 10U, 11U}},
                {{0U, 0U, 12U, 13U}},
                {{0U, 0U, 0U, 14U}},
                {{0U, 0U, 0U, 0U}},
            }};
            return bits[a][b];
        }

        [[nodiscard]] std::uint32_t plane_scroll_delay_raw(std::uint32_t plane,
                                                           std::uint16_t bplcon1) noexcept {
            return (plane & 1U) == 0U ? (bplcon1 & 0x000FU) : ((bplcon1 >> 4U) & 0x000FU);
        }

    } // namespace

    chip_metadata agnus::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "8367",
            .family = "agnus",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void agnus::reset(reset_kind /*kind*/) {
        const bool was_pal = is_pal_;
        dmacon_ = 0U;
        blitter_busy_ = false;
        blitter_zero_ = false;
        is_pal_ = was_pal;
        scanline_ = 0U;
        color_clock_ = 0U;
        long_frame_ = false;
        vblank_active_ = false;
        hblank_active_ = false;
        bitplane_pointer_.fill(0U);
        modulo_even_ = 0;
        modulo_odd_ = 0;
        bplcon0_ = 0U;
        bplcon1_ = 0U;
        bplcon2_ = 0U;
        clxcon_ = 0U;
        clxdat_ = 0U;
        diwstrt_ = 0U;
        diwstop_ = 0U;
        ddfstrt_ = 0U;
        ddfstop_ = 0U;
        sprite_ = {};
        cop1lc_ = 0U;
        cop2lc_ = 0U;
        copper_pc_ = 0U;
        copper_running_ = false;
        copper_danger_ = false;
        copper_delay_ = 0U;
        frame_index_ = 0U;
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void agnus::set_pal(bool is_pal) noexcept {
        is_pal_ = is_pal;
        // Snap the beam into range of the new region immediately.
        const std::uint32_t max_line = is_pal ? scanlines_pal : scanlines_ntsc;
        if (scanline_ >= max_line) {
            scanline_ = max_line - 1U;
        }
    }

    void agnus::tick(std::uint64_t cycles) {
        const std::uint32_t max_line = is_pal_ ? scanlines_pal : scanlines_ntsc;
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (color_clock_ == 0U) {
                if (scanline_ == 0U) {
                    begin_frame_render();
                }
                if (scanline_ == vblank_end_line(is_pal_) && dma_copper()) {
                    // Real OCS returns the Copper to COP1LC as vertical blank
                    // exits, after the CPU/vblank server has a chance to
                    // publish the display list for the active field.
                    copper_pc_ = cop1lc_;
                    copper_running_ = true;
                    copper_delay_ = 0U;
                }
                if (scanline_cb_) {
                    scanline_cb_(scanline_);
                }
            }
            // The copper walks during the visible field, one slot per clock.
            run_copper();
            if (cycle_cb_) {
                cycle_cb_();
            }
            if (++color_clock_ == color_clocks_per_line) {
                render_scanline(scanline_);
                color_clock_ = 0U;
                if (++scanline_ == max_line) {
                    scanline_ = 0U;
                    if ((bplcon0_ & bplcon0_lace) != 0U) {
                        long_frame_ = !long_frame_;
                    } else {
                        long_frame_ = false;
                    }
                    render_sprites(active_height());
                    ++frame_index_;
                    if (vblank_cb_) {
                        vblank_cb_(scanline_);
                    }
                }
            }
            refresh_blank_flags();
        }
    }

    void agnus::refresh_blank_flags() noexcept {
        // OCS HBlank is the start/end color-clock band of each line; VBlank
        // covers the top scanlines. Approximations sufficient to gate copper
        // WAITs and sprite-DMA windows.
        hblank_active_ = (color_clock_ < 18U) || (color_clock_ >= 208U);
        vblank_active_ = scanline_ < vblank_end_line(is_pal_);
    }

    // --- DMA controller ----------------------------------------------------

    void agnus::write_dmacon(std::uint16_t value) noexcept {
        const std::uint16_t payload = value & dmacon_writable;
        if ((value & dmacon_set) != 0U) {
            dmacon_ = static_cast<std::uint16_t>(dmacon_ | payload);
        } else {
            dmacon_ = static_cast<std::uint16_t>(dmacon_ & ~payload);
        }
        // DMA gating only grants or removes bus ownership. The Copper PC is
        // loaded at vertical blank or by the explicit COPJMP strobes.
        if (!dma_copper()) {
            copper_running_ = false;
            copper_delay_ = 0U;
        }
    }

    std::uint16_t agnus::read_dmaconr() const noexcept {
        std::uint16_t value = dmacon_ & dmacon_writable;
        if (blitter_busy_) {
            value = static_cast<std::uint16_t>(value | dmacon_bbusy);
        }
        if (blitter_zero_) {
            value = static_cast<std::uint16_t>(value | dmacon_bzero);
        }
        return value;
    }

    std::uint16_t agnus::read_vposr() const noexcept {
        std::uint16_t value = 0U;
        if ((bplcon0_ & bplcon0_lace) != 0U && long_frame_) {
            value |= 0x8000U;
        }
        value |= static_cast<std::uint16_t>((scanline_ >> 8U) & 0x0001U);
        return value;
    }

    std::uint16_t agnus::read_vhposr() const noexcept {
        auto value = static_cast<std::uint16_t>((scanline_ & 0x00FFU) << 8U);
        value = static_cast<std::uint16_t>(value | (color_clock_ & 0x00FFU));
        return value;
    }

    bool agnus::dma_master() const noexcept { return (dmacon_ & dmacon_dmaen) != 0U; }

    namespace {
        [[nodiscard]] bool dma_both(std::uint16_t dmacon, std::uint16_t channel_bit) noexcept {
            return (dmacon & agnus::dmacon_dmaen) != 0U && (dmacon & channel_bit) != 0U;
        }
    } // namespace

    bool agnus::dma_bitplane() const noexcept { return dma_both(dmacon_, dmacon_bplen); }
    bool agnus::dma_copper() const noexcept { return dma_both(dmacon_, dmacon_copen); }
    bool agnus::dma_blitter() const noexcept { return dma_both(dmacon_, dmacon_blten); }
    bool agnus::dma_sprite() const noexcept { return dma_both(dmacon_, dmacon_spren); }
    bool agnus::dma_disk() const noexcept { return dma_both(dmacon_, dmacon_dsken); }

    bool agnus::dma_audio(int channel) const noexcept {
        if (channel < 0 || channel > 3) {
            return false;
        }
        constexpr std::array<std::uint16_t, 4> channel_bits{dmacon_aud0en, dmacon_aud1en,
                                                            dmacon_aud2en, dmacon_aud3en};
        return dma_both(dmacon_, channel_bits[static_cast<std::size_t>(channel)]);
    }

    // --- display registers -------------------------------------------------

    agnus::display_dma_slot
    agnus::display_dma_slot_at(std::uint32_t instruction_cycles_before_access) const noexcept {
        const std::uint32_t elapsed_color_clocks =
            instruction_cycles_before_access / cpu_cycles_per_memory_slot;
        const std::uint32_t projected_clock_total = color_clock_ + elapsed_color_clocks;
        const std::uint32_t beam_line = scanline_ + (projected_clock_total / color_clocks_per_line);
        const std::uint32_t beam_clock = projected_clock_total % color_clocks_per_line;
        return display_dma_slot_at_beam(beam_line, beam_clock);
    }

    agnus::display_dma_slot
    agnus::display_dma_slot_at_beam(std::uint32_t beam_line,
                                    std::uint32_t beam_clock) const noexcept {
        const std::uint32_t planes = bitplane_count();
        if (!dma_bitplane() || planes == 0U) {
            return {};
        }

        const std::uint32_t diw_v_start = (diwstrt_ >> 8U) & 0xFFU;
        const std::uint32_t diw_v_stop_raw = (diwstop_ >> 8U) & 0xFFU;
        const std::uint32_t diw_v_stop =
            diw_v_stop_raw | (((diw_v_stop_raw & 0x80U) == 0U) ? 0x100U : 0U);
        if (beam_line < diw_v_start || beam_line >= diw_v_stop) {
            return {};
        }

        const std::uint32_t ddf_start = ddfstrt_ & 0xFFU;
        const std::uint32_t ddf_stop = ddfstop_ & 0xFFU;
        if (ddf_start >= color_clocks_per_line || ddf_stop < ddf_start) {
            return {};
        }

        const bool hires = hires_enabled();
        const std::uint32_t clocks_per_word = hires ? 4U : 8U;
        const std::uint32_t terminal_words = hires ? 2U : 1U;
        const std::uint32_t normal_words_per_line =
            ((ddf_stop - ddf_start) / clocks_per_word) + terminal_words;
        const std::uint32_t fetch_end =
            std::min(color_clocks_per_line, ddf_start + normal_words_per_line * clocks_per_word);
        if (beam_clock < ddf_start || beam_clock >= fetch_end) {
            return {};
        }

        return {
            .active = true,
            .hires = hires,
            .active_planes = planes,
            .group_offset = (beam_clock - ddf_start) % clocks_per_word,
            .beam_clock = beam_clock,
            .fetch_end = fetch_end,
        };
    }

    agnus::display_dma_wait
    agnus::display_dma_cpu_wait(std::uint32_t instruction_cycles_before_access) const noexcept {
        const display_dma_slot slot = display_dma_slot_at(instruction_cycles_before_access);
        if (!slot.active) {
            return {};
        }

        if (slot.hires) {
            if (slot.active_planes >= max_hires_bitplanes) {
                return {
                    (slot.fetch_end - slot.beam_clock) * cpu_cycles_per_memory_slot,
                };
            }
            if (slot.active_planes == 3U && (slot.group_offset == 2U || slot.group_offset == 3U)) {
                return {(4U - slot.group_offset) * cpu_cycles_per_memory_slot};
            }
            return {};
        }

        if (slot.active_planes < 5U) {
            return {};
        }
        if (slot.active_planes == 5U && (slot.group_offset == 5U || slot.group_offset == 6U)) {
            return {(7U - slot.group_offset) * cpu_cycles_per_memory_slot};
        }
        if (slot.active_planes >= 6U) {
            if (slot.group_offset == 1U || slot.group_offset == 2U) {
                return {(3U - slot.group_offset) * cpu_cycles_per_memory_slot};
            }
            if (slot.group_offset == 5U || slot.group_offset == 6U) {
                return {(7U - slot.group_offset) * cpu_cycles_per_memory_slot};
            }
        }
        return {};
    }

    std::uint32_t agnus::display_dma_cpu_wait_cycles(
        std::uint32_t instruction_cycles_before_access) const noexcept {
        return display_dma_cpu_wait(instruction_cycles_before_access).cycles;
    }

    agnus::sprite_dma_slot
    agnus::sprite_dma_slot_at(std::uint32_t instruction_cycles_before_access) const noexcept {
        if (!dma_sprite() || chip_ram_.empty()) {
            return {};
        }

        const std::uint32_t elapsed_color_clocks =
            instruction_cycles_before_access / cpu_cycles_per_memory_slot;
        const std::uint32_t projected_clock_total = color_clock_ + elapsed_color_clocks;
        const std::uint32_t beam_line = scanline_ + (projected_clock_total / color_clocks_per_line);
        const std::uint32_t beam_clock = projected_clock_total % color_clocks_per_line;
        if (beam_clock < sprite_dma_first_slot ||
            beam_clock >= sprite_dma_first_slot + sprite_dma_slot_count) {
            return {};
        }

        const std::uint32_t sprite =
            (beam_clock - sprite_dma_first_slot) / sprite_dma_slots_per_channel;
        if (sprite >= max_sprites ||
            display_dma_owns_memory_slot(display_dma_slot_at_beam(beam_line, beam_clock)) ||
            !sprite_dma_fetch_requested(sprite, beam_line)) {
            return {};
        }

        return {
            .active = true,
            .sprite = sprite,
            .beam_clock = beam_clock,
        };
    }

    std::uint32_t agnus::sprite_dma_cpu_wait_cycles(
        std::uint32_t instruction_cycles_before_access) const noexcept {
        const sprite_dma_slot slot = sprite_dma_slot_at(instruction_cycles_before_access);
        if (!slot.active) {
            return 0U;
        }
        const std::uint32_t channel_end =
            sprite_dma_first_slot + (slot.sprite + 1U) * sprite_dma_slots_per_channel;
        return (channel_end - slot.beam_clock) * cpu_cycles_per_memory_slot;
    }

    void agnus::set_bitplane_pointer(std::uint32_t plane, std::uint32_t byte_address) noexcept {
        if (plane < max_bitplanes) {
            bitplane_pointer_[plane] = byte_address & chip_address_mask;
        }
    }

    void agnus::write_bitplane_pointer_word(std::uint32_t plane, bool high,
                                            std::uint16_t value) noexcept {
        if (plane >= max_bitplanes) {
            return;
        }
        if (high) {
            bitplane_pointer_[plane] = ((static_cast<std::uint32_t>(value) & chip_address_high_mask)
                                        << 16U) |
                                       (bitplane_pointer_[plane] & 0x0000FFFEU);
        } else {
            bitplane_pointer_[plane] = (bitplane_pointer_[plane] & 0x001F0000U) |
                                       (static_cast<std::uint32_t>(value) & 0x0000FFFEU);
        }
    }

    std::uint32_t agnus::bitplane_pointer(std::uint32_t plane) const noexcept {
        return plane < max_bitplanes ? bitplane_pointer_[plane] : 0U;
    }

    void agnus::set_sprite_pointer(std::uint32_t sprite, std::uint32_t byte_address) noexcept {
        if (sprite < max_sprites) {
            sprite_[sprite].pointer = byte_address & chip_address_mask;
        }
    }

    std::uint32_t agnus::sprite_pointer(std::uint32_t sprite) const noexcept {
        return sprite < max_sprites ? sprite_[sprite].pointer : 0U;
    }

    void agnus::write_sprite_pointer_word(std::uint32_t sprite, bool high,
                                          std::uint16_t value) noexcept {
        if (sprite >= max_sprites) {
            return;
        }
        if (high) {
            sprite_[sprite].pointer = ((static_cast<std::uint32_t>(value) & chip_address_high_mask)
                                       << 16U) |
                                      (sprite_[sprite].pointer & 0x0000FFFEU);
        } else {
            sprite_[sprite].pointer = (sprite_[sprite].pointer & 0x001F0000U) |
                                      (static_cast<std::uint32_t>(value) & 0x0000FFFEU);
        }
    }

    void agnus::write_sprite_pos(std::uint32_t sprite, std::uint16_t value) noexcept {
        if (sprite < max_sprites) {
            sprite_[sprite].pos = value;
            sprite_[sprite].armed = false;
        }
    }

    void agnus::write_sprite_ctl(std::uint32_t sprite, std::uint16_t value) noexcept {
        if (sprite < max_sprites) {
            sprite_[sprite].ctl = value;
            sprite_[sprite].armed = false;
        }
    }

    void agnus::write_sprite_data_a(std::uint32_t sprite, std::uint16_t value) noexcept {
        if (sprite < max_sprites) {
            sprite_[sprite].data_a = value;
            sprite_[sprite].armed = true;
        }
    }

    void agnus::write_sprite_data_b(std::uint32_t sprite, std::uint16_t value) noexcept {
        if (sprite < max_sprites) {
            sprite_[sprite].data_b = value;
        }
    }

    std::uint16_t agnus::read_clxdat() noexcept {
        const std::uint16_t value = static_cast<std::uint16_t>(clxdat_ & 0x7FFFU);
        clxdat_ = 0U;
        return value;
    }

    std::uint32_t agnus::bitplane_count() const noexcept {
        const std::uint32_t bpu = (bplcon0_ >> 12U) & 0x07U;
        return std::min<std::uint32_t>(bpu, hires_enabled() ? max_hires_bitplanes : max_bitplanes);
    }

    bool agnus::display_dma_owns_memory_slot(const display_dma_slot& slot) const noexcept {
        if (!slot.active) {
            return false;
        }

        if (slot.hires) {
            if (slot.active_planes >= max_hires_bitplanes) {
                return true;
            }
            return slot.active_planes == 3U && (slot.group_offset == 2U || slot.group_offset == 3U);
        }

        if (slot.active_planes == 5U) {
            return slot.group_offset == 5U || slot.group_offset == 6U;
        }
        if (slot.active_planes >= 6U) {
            return slot.group_offset == 1U || slot.group_offset == 2U || slot.group_offset == 5U ||
                   slot.group_offset == 6U;
        }
        return false;
    }

    bool agnus::display_dma_copper_blocked() const noexcept {
        return display_dma_owns_memory_slot(display_dma_slot_at(0U));
    }

    bool agnus::display_dma_steals_sprite_word(std::uint32_t sprite, std::uint32_t beam_line,
                                               std::uint32_t word_index) const noexcept {
        if (sprite >= max_sprites || word_index >= sprite_dma_slots_per_channel) {
            return false;
        }

        const std::uint32_t slot =
            sprite_dma_first_slot + sprite * sprite_dma_slots_per_channel + word_index;
        return display_dma_owns_memory_slot(display_dma_slot_at_beam(beam_line, slot));
    }

    bool agnus::display_dma_steals_sprite_slot(std::uint32_t sprite,
                                               std::uint32_t beam_line) const noexcept {
        if (sprite >= max_sprites) {
            return false;
        }

        for (std::uint32_t offset = 0U; offset < sprite_dma_slots_per_channel; ++offset) {
            if (display_dma_steals_sprite_word(sprite, beam_line, offset)) {
                return true;
            }
        }
        return false;
    }

    bool agnus::sprite_dma_fetch_requested(std::uint32_t sprite,
                                           std::uint32_t beam_line) const noexcept {
        if (sprite >= max_sprites || !dma_sprite() || chip_ram_.empty()) {
            return false;
        }

        std::uint32_t ptr = sprite_[sprite].pointer;
        std::uint32_t previous_visible_stop = 0U;
        std::uint32_t previous_control_fetch_line = 0U;
        bool has_visible_block = false;
        bool has_control_fetch_floor = false;
        for (std::uint32_t block = 0U; block < max_sprite_dma_blocks; ++block) {
            const std::uint16_t pos = chip_word(ptr);
            ptr = (ptr + 2U) & chip_address_mask;
            const std::uint16_t ctl = chip_word(ptr);
            ptr = (ptr + 2U) & chip_address_mask;
            if (pos == 0U && ctl == 0U) {
                return false;
            }

            const std::uint32_t start = sprite_vstart(pos, ctl);
            const std::uint32_t stop = sprite_vstop(ctl);
            if (stop < start) {
                return false;
            }
            if (has_control_fetch_floor && start < previous_control_fetch_line) {
                return false;
            }
            if (beam_line < start) {
                return false;
            }
            if (stop == start) {
                if (beam_line == start) {
                    return true;
                }
                previous_control_fetch_line = start;
                has_control_fetch_floor = true;
                continue;
            }
            if (has_visible_block && start <= previous_visible_stop) {
                ptr = (ptr + (stop - start) * 4U) & chip_address_mask;
                previous_visible_stop = std::max(previous_visible_stop, stop);
                previous_control_fetch_line = std::max(previous_control_fetch_line, stop);
                has_control_fetch_floor = true;
                continue;
            }
            if (beam_line < stop) {
                return true;
            }
            if (beam_line == stop) {
                return true;
            }

            const std::uint32_t data_lines = stop - start;
            ptr = (ptr + data_lines * 4U) & chip_address_mask;
            previous_visible_stop = stop;
            previous_control_fetch_line = stop;
            has_visible_block = true;
            has_control_fetch_floor = true;
        }
        return false;
    }

    bool agnus::sprite_dma_copper_blocked() const noexcept { return sprite_dma_slot_at(0U).active; }

    // --- copper ------------------------------------------------------------

    void agnus::set_copper_address_mask(std::uint32_t mask) noexcept {
        const std::uint32_t normalized = mask & chip_address_mask;
        copper_address_mask_ = normalized == 0U ? ocs_copper_address_mask : normalized;
        cop1lc_ &= copper_address_mask_;
        cop2lc_ &= copper_address_mask_;
        copper_pc_ &= copper_address_mask_;
    }

    void agnus::write_cop1lc(std::uint32_t value) noexcept {
        cop1lc_ = value & copper_address_mask_;
    }

    void agnus::write_cop2lc(std::uint32_t value) noexcept {
        cop2lc_ = value & copper_address_mask_;
    }

    void agnus::strobe_copjmp1() noexcept {
        copper_pc_ = cop1lc_;
        copper_running_ = dma_copper();
        copper_delay_ = 0U;
    }

    void agnus::strobe_copjmp2() noexcept {
        copper_pc_ = cop2lc_;
        copper_running_ = dma_copper();
        copper_delay_ = 0U;
    }

    std::uint16_t agnus::chip_word(std::uint32_t byte_address) const noexcept {
        if (chip_ram_.size() < 2U) {
            return 0U;
        }
        const std::size_t a = mirrored_chip_word_address(chip_ram_, byte_address);
        // Big-endian word.
        return static_cast<std::uint16_t>((chip_ram_[a] << 8U) | chip_ram_[a + 1U]);
    }

    void agnus::run_copper() noexcept {
        if (!copper_running_ || !dma_copper()) {
            copper_running_ = dma_copper() && copper_running_;
            copper_delay_ = 0U;
            return;
        }
        if (chip_ram_.empty()) {
            return;
        }
        if (display_dma_copper_blocked()) {
            return;
        }
        if (sprite_dma_copper_blocked()) {
            return;
        }
        if (copper_delay_ != 0U) {
            --copper_delay_;
            return;
        }
        // One instruction (two words) per invocation when not blocked. The
        // beam comparison uses the raw 8-bit VP and 7-bit HP of the current
        // position, exactly as the reference copper.
        const std::uint16_t beam_vp = static_cast<std::uint16_t>(scanline_ & 0x00FFU);
        const std::uint16_t beam_hp = static_cast<std::uint16_t>(color_clock_ & 0x00FEU);

        const std::uint16_t ir1 = chip_word(copper_pc_);
        const std::uint16_t ir2 = chip_word(copper_pc_ + 2U);
        const std::uint32_t instruction_pc = copper_pc_;
        const bool trace_instruction = copper_trace_matches(instruction_pc, copper_address_mask_);

        const bool is_move = (ir1 & 0x0001U) == 0U;
        if (is_move) {
            const std::uint16_t reg_addr = static_cast<std::uint16_t>(ir1 & 0x01FEU);
            const std::uint32_t previous_pc = copper_pc_;
            const bool write_allowed = copper_can_write_register(reg_addr, copper_danger_);
            if (write_allowed) {
                apply_copper_move(reg_addr, ir2);
            }
            if (copper_pc_ == previous_pc) {
                copper_pc_ = (copper_pc_ + 4U) & copper_address_mask_;
            }
            if (copper_running_) {
                copper_delay_ = copper_move_skip_cycles - 1U;
            }
            if (trace_instruction) {
                std::fprintf(stderr,
                             "[agnus-copper] beam=%03u:%03u pc=%06X ir1=%04X ir2=%04X "
                             "MOVE reg=%03X allowed=%u next=%06X delay=%u\n",
                             scanline_, color_clock_, instruction_pc, ir1, ir2, reg_addr,
                             write_allowed ? 1U : 0U, copper_pc_, copper_delay_);
            }
            return;
        }

        // WAIT / SKIP. The upper byte of IR2 is the vertical compare-enable
        // mask as programmed; Kickstart uses zero-masked impossible waits to
        // park secondary copper lists.
        const std::uint16_t wait_vp = static_cast<std::uint16_t>((ir1 >> 8U) & 0x00FFU);
        const std::uint16_t wait_hp = static_cast<std::uint16_t>(ir1 & 0x00FEU);
        const std::uint16_t wait_ve = static_cast<std::uint16_t>((ir2 >> 8U) & 0x00FFU);
        const std::uint16_t wait_he = static_cast<std::uint16_t>(ir2 & 0x00FEU);
        const bool wait_bfd = (ir2 & copper_bfd_mask) != 0U;
        const bool is_skip = (ir2 & 0x0001U) != 0U;

        const std::uint16_t vp_beam = static_cast<std::uint16_t>(beam_vp & wait_ve);
        const std::uint16_t vp_target = static_cast<std::uint16_t>(wait_vp & wait_ve);
        const std::uint16_t hp_beam = static_cast<std::uint16_t>(beam_hp & wait_he);
        const std::uint16_t hp_target = static_cast<std::uint16_t>(wait_hp & wait_he);

        bool past = true;
        if (vp_beam < vp_target) {
            past = false;
        } else if (vp_beam == vp_target && hp_beam < hp_target) {
            past = false;
        } else if (!wait_bfd && blitter_busy_) {
            past = false;
        }
        if (trace_instruction) {
            std::fprintf(stderr,
                         "[agnus-copper] beam=%03u:%03u pc=%06X ir1=%04X ir2=%04X "
                         "%s vp=%02X/%02X ve=%02X hp=%02X/%02X he=%02X bfd=%u "
                         "past=%u blt=%u\n",
                         scanline_, color_clock_, instruction_pc, ir1, ir2,
                         is_skip ? "SKIP" : "WAIT", vp_beam, vp_target, wait_ve, hp_beam,
                         hp_target, wait_he, wait_bfd ? 1U : 0U, past ? 1U : 0U,
                         blitter_busy_ ? 1U : 0U);
        }

        if (is_skip) {
            copper_pc_ = (copper_pc_ + 4U) & copper_address_mask_;
            if (past) {
                // Skip the next instruction's two words as well.
                copper_pc_ = (copper_pc_ + 4U) & copper_address_mask_;
            }
            copper_delay_ = copper_move_skip_cycles - 1U;
            return;
        }
        // WAIT: advance past the instruction only when the target is reached;
        // otherwise stall (re-evaluate on the next clock).
        if (past) {
            copper_pc_ = (copper_pc_ + 4U) & copper_address_mask_;
            copper_delay_ = copper_wait_wake_cycles - 1U;
        }
    }

    void agnus::apply_copper_move(std::uint16_t reg_addr, std::uint16_t value) noexcept {
        if (custom_write_cb_) {
            custom_write_cb_(reg_addr, value);
            return;
        }

        switch (reg_addr) {
        case reg_dmacon:
            write_dmacon(value);
            return;
        case reg_bplcon0:
            bplcon0_ = value;
            return;
        case reg_bplcon1:
            set_bplcon1(value);
            return;
        case reg_bplcon2:
            set_bplcon2(value);
            return;
        case reg_bpl1mod:
            modulo_odd_ = static_cast<std::int16_t>(value);
            return;
        case reg_bpl2mod:
            modulo_even_ = static_cast<std::int16_t>(value);
            return;
        case reg_diwstrt:
            diwstrt_ = value;
            return;
        case reg_diwstop:
            diwstop_ = value;
            return;
        case reg_ddfstrt:
            ddfstrt_ = value;
            return;
        case reg_ddfstop:
            ddfstop_ = value;
            return;
        case reg_clxcon:
            write_clxcon(value);
            return;
        default:
            break;
        }
        // Bitplane pointers: BPL1PTH..BPL6PTL occupy a contiguous run of
        // high/low word pairs (4 bytes apart). The high word is the upper
        // bits, the low word the lower bits of the 20-bit chip-RAM pointer.
        if (reg_addr >= reg_bplpth_base && reg_addr < reg_bplpth_base + max_bitplanes * 4U) {
            const std::uint32_t idx = (reg_addr - reg_bplpth_base) / 4U;
            const bool is_high = ((reg_addr - reg_bplpth_base) & 0x02U) == 0U;
            write_bitplane_pointer_word(idx, is_high, value);
            return;
        }
        if (reg_addr >= reg_sprpth_base && reg_addr < reg_sprpth_base + max_sprites * 4U) {
            const std::uint32_t idx = (reg_addr - reg_sprpth_base) / 4U;
            const bool is_high = ((reg_addr - reg_sprpth_base) & 0x02U) == 0U;
            write_sprite_pointer_word(idx, is_high, value);
            return;
        }
        if (reg_addr >= reg_sprpos_base && reg_addr < reg_sprpos_base + max_sprites * 8U) {
            const std::uint32_t idx = (reg_addr - reg_sprpos_base) / 8U;
            const auto offset = static_cast<std::uint16_t>((reg_addr - reg_sprpos_base) & 0x06U);
            switch (offset) {
            case 0x00U:
                write_sprite_pos(idx, value);
                return;
            case 0x02U:
                write_sprite_ctl(idx, value);
                return;
            case 0x04U:
                write_sprite_data_a(idx, value);
                return;
            case 0x06U:
                write_sprite_data_b(idx, value);
                return;
            default:
                return;
            }
        }
        // Colour palette: COLOR00..COLOR31, 2 bytes apart. Mirror into the
        // attached palette span only when writable storage is provided; the
        // render path reads the palette span directly, so a copper MOVE to a
        // colour register is honoured by the board mapping that span. When no
        // mutable palette is wired the MOVE is a no-op here (the board owns
        // the colour RAM cell). Nothing to do in this read-only model.
        (void)reg_color_base;
    }

    // --- rendering ---------------------------------------------------------

    std::uint16_t agnus::palette_word(std::uint32_t index) const noexcept {
        const std::size_t a = static_cast<std::size_t>(index) * 2U;
        if (a + 1U >= palette_.size()) {
            return 0U;
        }
        // Big-endian colour word.
        return static_cast<std::uint16_t>((palette_[a] << 8U) | palette_[a + 1U]);
    }

    std::uint32_t agnus::color_to_rgb(std::uint16_t color12) noexcept {
        const std::uint32_t r = expand4(static_cast<std::uint8_t>((color12 >> 8U) & 0x0FU));
        const std::uint32_t g = expand4(static_cast<std::uint8_t>((color12 >> 4U) & 0x0FU));
        const std::uint32_t b = expand4(static_cast<std::uint8_t>(color12 & 0x0FU));
        return (r << 16U) | (g << 8U) | b;
    }

    void agnus::begin_frame_render() noexcept {
        std::fill(bitplane_sample_.begin(), bitplane_sample_.end(), std::uint8_t{0U});
        std::fill(playfield_sample_.begin(), playfield_sample_.end(), std::uint8_t{0U});
        std::fill(sprite_sample_.begin(), sprite_sample_.end(), std::uint16_t{0U});
        for (std::uint32_t& px : pixels_) {
            px = 0U;
        }
    }

    void agnus::render_scanline(std::uint32_t beam_line) noexcept {
        const std::uint32_t height = active_height();
        if (beam_line < display_line_origin || beam_line >= display_line_origin + height) {
            return;
        }

        const std::uint32_t line = beam_line - display_line_origin;
        const std::uint32_t width = active_width();
        constexpr std::uint32_t stride = framebuffer_stride;
        const std::uint32_t backdrop = color_to_rgb(palette_word(0U));
        const std::size_t row = static_cast<std::size_t>(line) * stride;
        for (std::uint32_t x = 0; x < width; ++x) {
            pixels_[row + x] = backdrop;
        }

        const std::uint32_t planes = bitplane_count();
        const bool draw_bitplanes = planes != 0U && dma_bitplane() && !chip_ram_.empty();
        if (!draw_bitplanes) {
            return;
        }

        const bool ham = (bplcon0_ & 0x0800U) != 0U && planes >= 5U;
        const bool dual_playfield = (bplcon0_ & 0x0400U) != 0U && !ham;
        const bool ehb = !ham && !dual_playfield && planes == 6U;

        // Display window: DIWSTRT/DIWSTOP give the visible vertical band; the
        // horizontal start positions the first fetched word. The data-fetch
        // window (DDFSTRT/DDFSTOP) bounds the per-line word count: low-res
        // fetches at 8-clock granularity, high-res at 4-clock granularity
        // with the second terminal word included by the OCS relationship.
        const std::uint32_t diw_v_start = (diwstrt_ >> 8U) & 0xFFU;
        const std::uint32_t diw_v_stop_raw = (diwstop_ >> 8U) & 0xFFU;
        // DIWSTOP V bit 8 is the complement of bit 7 (so stops >= 0x100 are
        // expressible); reconstruct the 9-bit stop.
        const std::uint32_t diw_v_stop =
            diw_v_stop_raw | (((diw_v_stop_raw & 0x80U) == 0U) ? 0x100U : 0U);

        const std::uint32_t ddf_start = ddfstrt_ & 0xFFU;
        const std::uint32_t ddf_stop = ddfstop_ & 0xFFU;
        const bool hires = hires_enabled();
        const std::uint32_t pixel_scale = hires ? 2U : 1U;
        constexpr std::uint32_t diw_h_origin = 0x81U;
        const std::int32_t display_start_x =
            (static_cast<std::int32_t>(diwstrt_ & 0x00FFU) -
             static_cast<std::int32_t>(diw_h_origin)) *
            static_cast<std::int32_t>(pixel_scale);
        const std::int32_t display_stop_x =
            (static_cast<std::int32_t>((diwstop_ & 0x00FFU) + 0x100U) -
             static_cast<std::int32_t>(diw_h_origin)) *
            static_cast<std::int32_t>(pixel_scale);
        const std::uint32_t display_left = static_cast<std::uint32_t>(
            std::clamp(display_start_x, 0, static_cast<std::int32_t>(width)));
        const std::uint32_t display_right = static_cast<std::uint32_t>(
            std::clamp(display_stop_x, 0, static_cast<std::int32_t>(width)));
        std::uint32_t words_per_line = 0U;
        if (ddf_stop >= ddf_start) {
            const std::uint32_t clocks_per_word = hires ? 4U : 8U;
            const std::uint32_t terminal_words = hires ? 2U : 1U;
            words_per_line = ((ddf_stop - ddf_start) / clocks_per_word) + terminal_words;
        }

        if (beam_line < diw_v_start || beam_line >= diw_v_stop ||
            display_left >= display_right) {
            return;
        }

        std::array<std::uint8_t, visible_width_hires> line_sample{};
        std::uint32_t ham_hold = backdrop;
        const std::int32_t data_fetch_origin_x =
            hires ? (static_cast<std::int32_t>(ddf_start) - 0x3C) * 4
                  : (static_cast<std::int32_t>(ddf_start) - 0x38) * 2;
        for (std::uint32_t word = 0; word < words_per_line; ++word) {
            // Fetch one word per active plane for this cell. BPLCON1 delays
            // the odd/even playfield serializers independently.
            for (std::uint32_t p = 0; p < planes; ++p) {
                const std::uint16_t data = chip_word(bitplane_pointer_[p]);
                bitplane_pointer_[p] = (bitplane_pointer_[p] + 2U) & chip_address_mask;
                const std::uint32_t raw_delay = plane_scroll_delay_raw(p, bplcon1_);
                const std::uint32_t delay = hires ? raw_delay * 2U : raw_delay;
                const std::int32_t base_x =
                    data_fetch_origin_x + static_cast<std::int32_t>(word * 16U + delay);
                for (std::uint32_t pixel = 0; pixel < 16U; ++pixel) {
                    const std::uint32_t bit = 15U - pixel;
                    const std::int32_t x = base_x + static_cast<std::int32_t>(pixel);
                    if (x < 0 || x >= static_cast<std::int32_t>(width)) {
                        continue;
                    }
                    line_sample[static_cast<std::uint32_t>(x)] = static_cast<std::uint8_t>(
                        line_sample[static_cast<std::uint32_t>(x)] |
                        (((data >> bit) & 1U) << p));
                }
            }
        }
        for (std::uint32_t x = display_left; x < display_right; ++x) {
            const std::size_t out = static_cast<std::size_t>(line) * stride + x;
            const std::uint32_t index = line_sample[x];
            bitplane_sample_[out] = static_cast<std::uint8_t>(index & 0x3FU);

            std::uint32_t rgb = backdrop;
            if (dual_playfield) {
                std::uint32_t playfield1 = 0U;
                std::uint32_t playfield2 = 0U;
                for (std::uint32_t p = 0; p < planes; ++p) {
                    const std::uint32_t plane_bit = (index >> p) & 1U;
                    if ((p & 1U) == 0U) {
                        playfield1 |= plane_bit << (p >> 1U);
                    } else {
                        playfield2 |= plane_bit << (p >> 1U);
                    }
                }
                if (playfield1 != 0U) {
                    playfield_sample_[out] =
                        static_cast<std::uint8_t>(playfield_sample_[out] | pf1_sample);
                }
                if (playfield2 != 0U) {
                    playfield_sample_[out] =
                        static_cast<std::uint8_t>(playfield_sample_[out] | pf2_sample);
                }
                const bool pf2_front = (bplcon2_ & 0x0040U) != 0U;
                if (playfield2 != 0U && (playfield1 == 0U || pf2_front)) {
                    rgb = color_to_rgb(palette_word(8U + playfield2));
                } else if (playfield1 != 0U) {
                    rgb = color_to_rgb(palette_word(playfield1));
                } else if (playfield2 != 0U) {
                    rgb = color_to_rgb(palette_word(8U + playfield2));
                }
            } else if (ham) {
                const std::uint32_t command = (index >> 4U) & 0x03U;
                const std::uint32_t nibble = index & 0x0FU;
                const std::uint32_t channel = expand4(static_cast<std::uint8_t>(nibble));
                switch (command) {
                case 0U:
                    ham_hold = color_to_rgb(palette_word(nibble));
                    break;
                case 1U:
                    ham_hold = (ham_hold & 0x00FFFF00U) | channel;
                    break;
                case 2U:
                    ham_hold = (ham_hold & 0x0000FFFFU) | (channel << 16U);
                    break;
                case 3U:
                    ham_hold = (ham_hold & 0x00FF00FFU) | (channel << 8U);
                    break;
                default:
                    break;
                }
                rgb = ham_hold;
                if (index != 0U) {
                    playfield_sample_[out] =
                        static_cast<std::uint8_t>(playfield_sample_[out] | pf2_sample);
                }
            } else if (ehb) {
                rgb = color_to_rgb(palette_word(index & 0x1FU));
                if ((index & 0x20U) != 0U) {
                    rgb = (rgb >> 1U) & 0x007F7F7FU;
                }
                if (index != 0U) {
                    playfield_sample_[out] =
                        static_cast<std::uint8_t>(playfield_sample_[out] | pf2_sample);
                }
            } else {
                // Colour index 0 already painted as the backdrop.
                if (index == 0U) {
                    continue;
                }
                rgb = color_to_rgb(palette_word(index));
                playfield_sample_[out] =
                    static_cast<std::uint8_t>(playfield_sample_[out] | pf2_sample);
            }
            pixels_[out] = rgb;
        }

        // End-of-line modulo: odd planes (1,3,5 => pointer indices 0,2,4)
        // add BPL1MOD; even planes add BPL2MOD.
        for (std::uint32_t p = 0; p < planes; ++p) {
            const std::int16_t modulo = ((p & 1U) == 0U) ? modulo_odd_ : modulo_even_;
            bitplane_pointer_[p] =
                (static_cast<std::uint32_t>(static_cast<std::int32_t>(bitplane_pointer_[p]) +
                                            modulo)) &
                chip_address_mask;
        }
    }

    void agnus::render_sprites(std::uint32_t /*height*/) noexcept {
        const std::uint32_t width = active_width();
        constexpr std::uint32_t stride = framebuffer_stride;
        const bool dual_playfield = (bplcon0_ & 0x0400U) != 0U && (bplcon0_ & 0x0800U) == 0U;
        const std::uint32_t planes = bitplane_count();
        const auto sprite_group_bits = [](std::uint16_t sprites) noexcept {
            std::uint8_t groups = 0U;
            for (std::uint32_t group = 0U; group < max_sprites / 2U; ++group) {
                const std::uint32_t even_sprite = group * 2U;
                const std::uint32_t odd_sprite = even_sprite + 1U;
                if (sprite_value(sprites, even_sprite) != 0U ||
                    sprite_value(sprites, odd_sprite) != 0U) {
                    groups = static_cast<std::uint8_t>(groups | (1U << group));
                }
            }
            return groups;
        };
        const auto sprite_above_playfield = [](std::uint8_t sprite_groups,
                                               std::uint8_t priority) noexcept {
            const std::uint8_t clamped = std::min<std::uint8_t>(priority, 4U);
            for (std::uint32_t group = 0U; group < clamped; ++group) {
                if ((sprite_groups & (1U << group)) != 0U) {
                    return true;
                }
            }
            return false;
        };
        const auto dual_playfield_rgb = [&](std::uint8_t sample, bool second) noexcept {
            std::uint32_t value = 0U;
            for (std::uint32_t plane = second ? 1U : 0U; plane < planes; plane += 2U) {
                value |= ((sample >> plane) & 0x01U) << (plane >> 1U);
            }
            return color_to_rgb(palette_word(second ? 8U + value : value));
        };
        std::fill(sprite_sample_.begin(), sprite_sample_.end(), std::uint16_t{0U});
        for (std::uint32_t i = max_sprites; i > 0U; --i) {
            const std::uint32_t sprite = i - 1U;
            if (dma_sprite() && !chip_ram_.empty()) {
                render_dma_sprite(sprite);
            } else if (sprite_[sprite].armed) {
                render_manual_sprite(sprite);
            }
        }

        for (std::uint32_t y = 0; y < active_height(); ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t out = static_cast<std::size_t>(y) * stride + x;
                const std::uint16_t sprites = sprite_sample_[out];
                const std::uint8_t collision_groups =
                    sprites != 0U ? collision_sprite_groups(sprites) : 0U;
                if (collision_groups != 0U || bitplane_sample_[out] != 0U) {
                    latch_collisions(collision_groups, bitplane_sample_[out]);
                }
                const std::uint8_t playfields = playfield_sample_[out];
                if (dual_playfield && playfields != 0U) {
                    const std::uint8_t sprite_groups = sprite_group_bits(sprites);
                    const std::uint8_t pf1_priority = static_cast<std::uint8_t>(bplcon2_ & 0x0007U);
                    const std::uint8_t pf2_priority =
                        static_cast<std::uint8_t>((bplcon2_ >> 3U) & 0x0007U);
                    const bool pf1_visible = (playfields & pf1_sample) != 0U &&
                                             !sprite_above_playfield(sprite_groups, pf1_priority);
                    const bool pf2_visible = (playfields & pf2_sample) != 0U &&
                                             !sprite_above_playfield(sprite_groups, pf2_priority);
                    const bool pf2_front = (bplcon2_ & 0x0040U) != 0U;

                    if ((pf2_front && pf2_visible) || (!pf1_visible && pf2_visible)) {
                        pixels_[out] = dual_playfield_rgb(bitplane_sample_[out], true);
                        continue;
                    }
                    if (pf1_visible) {
                        pixels_[out] = dual_playfield_rgb(bitplane_sample_[out], false);
                        continue;
                    }
                }
                if (sprites == 0U) {
                    continue;
                }

                for (std::uint32_t group = 0U; group < max_sprites / 2U; ++group) {
                    const std::uint32_t even_sprite = group * 2U;
                    const std::uint32_t odd_sprite = even_sprite + 1U;
                    const std::uint32_t even_value = sprite_value(sprites, even_sprite);
                    const std::uint32_t odd_value = sprite_value(sprites, odd_sprite);
                    const bool attached = (sprite_[odd_sprite].ctl & sprite_attach) != 0U;

                    std::uint32_t color_index = 0U;
                    if (attached && (even_value != 0U || odd_value != 0U)) {
                        const std::uint32_t attached_value = even_value | (odd_value << 2U);
                        if (attached_value == 0U) {
                            continue;
                        }
                        color_index = 16U + attached_value;
                    } else if (even_value != 0U) {
                        color_index = 16U + group * 4U + even_value;
                    } else if (odd_value != 0U) {
                        color_index = 16U + group * 4U + odd_value;
                    } else {
                        continue;
                    }

                    if (!dual_playfield && playfield_blocks_sprite(playfields, group)) {
                        break;
                    }
                    pixels_[out] = color_to_rgb(palette_word(color_index));
                    break;
                }
            }
        }
    }

    void agnus::render_dma_sprite(std::uint32_t sprite) noexcept {
        std::uint32_t ptr = sprite_[sprite].pointer;
        std::uint32_t previous_visible_stop = 0U;
        std::uint32_t previous_control_fetch_line = 0U;
        bool has_visible_block = false;
        bool has_control_fetch_floor = false;

        // A sprite DMA list can reuse the same channel by placing another
        // POS/CTL pair after the descriptor words. Cap traversal so corrupt
        // chip RAM cannot stall the frame renderer.
        for (std::uint32_t block = 0U; block < max_sprite_dma_blocks; ++block) {
            sprite_state control{};
            control.pos = chip_word(ptr);
            ptr = (ptr + 2U) & chip_address_mask;
            control.ctl = chip_word(ptr);
            ptr = (ptr + 2U) & chip_address_mask;
            if (control.pos == 0U && control.ctl == 0U) {
                sprite_[sprite].pointer = ptr;
                return;
            }

            const std::uint32_t start = sprite_vstart(control.pos, control.ctl);
            const std::uint32_t stop = sprite_vstop(control.ctl);
            if (stop < start) {
                sprite_[sprite].pointer = ptr;
                return;
            }
            if (has_control_fetch_floor && start < previous_control_fetch_line) {
                sprite_[sprite].pointer = ptr;
                return;
            }
            if (stop == start) {
                if (display_dma_steals_sprite_word(sprite, start, 0U)) {
                    sprite_[sprite].pointer = ptr;
                    return;
                }
                if (display_dma_steals_sprite_word(sprite, start, 1U)) {
                    sprite_[sprite].pointer = (ptr + 2U) & chip_address_mask;
                    return;
                }
                previous_control_fetch_line = start;
                has_control_fetch_floor = true;
                continue;
            }

            const std::int32_t x = sprite_visible_x(control.pos, control.ctl);
            if (has_visible_block && start <= previous_visible_stop) {
                ptr = (ptr + (stop - start) * 4U) & chip_address_mask;
                previous_visible_stop = std::max(previous_visible_stop, stop);
                previous_control_fetch_line = std::max(previous_control_fetch_line, stop);
                has_control_fetch_floor = true;
                continue;
            }
            for (std::uint32_t beam_line = start; beam_line < stop; ++beam_line) {
                if (display_dma_steals_sprite_word(sprite, beam_line, 0U)) {
                    sprite_[sprite].pointer = ptr;
                    return;
                }
                const std::uint16_t data_a = chip_word(ptr);
                ptr = (ptr + 2U) & chip_address_mask;
                if (display_dma_steals_sprite_word(sprite, beam_line, 1U)) {
                    sprite_[sprite].pointer = ptr;
                    return;
                }
                const std::uint16_t data_b = chip_word(ptr);
                ptr = (ptr + 2U) & chip_address_mask;
                if (beam_line < display_line_origin) {
                    continue;
                }
                const std::uint32_t visible_line = beam_line - display_line_origin;
                if (visible_line >= active_height()) {
                    continue;
                }
                render_sprite_line(sprite, x, visible_line, data_a, data_b);
            }
            if (display_dma_steals_sprite_word(sprite, stop, 0U)) {
                sprite_[sprite].pointer = ptr;
                return;
            }
            if (display_dma_steals_sprite_word(sprite, stop, 1U)) {
                sprite_[sprite].pointer = (ptr + 2U) & chip_address_mask;
                return;
            }
            previous_visible_stop = stop;
            previous_control_fetch_line = stop;
            has_visible_block = true;
            has_control_fetch_floor = true;
        }
        sprite_[sprite].pointer = ptr;
    }

    void agnus::render_manual_sprite(std::uint32_t sprite) noexcept {
        const sprite_state& s = sprite_[sprite];
        const std::uint32_t start = sprite_vstart(s.pos, s.ctl);
        const std::uint32_t stop = sprite_vstop(s.ctl);
        if (stop <= start) {
            return;
        }
        const std::int32_t x = sprite_visible_x(s.pos, s.ctl);
        for (std::uint32_t beam_line = start; beam_line < stop; ++beam_line) {
            if (beam_line < display_line_origin) {
                continue;
            }
            const std::uint32_t visible_line = beam_line - display_line_origin;
            if (visible_line >= active_height()) {
                continue;
            }
            render_sprite_line(sprite, x, visible_line, s.data_a, s.data_b);
        }
    }

    void agnus::render_sprite_line(std::uint32_t sprite, std::int32_t x, std::uint32_t visible_line,
                                   std::uint16_t data_a, std::uint16_t data_b) noexcept {
        const std::uint32_t width = active_width();
        constexpr std::uint32_t stride = framebuffer_stride;
        const std::int32_t pixel_scale = hires_enabled() ? 2 : 1;
        for (std::uint32_t pixel = 0; pixel < sprite_width; ++pixel) {
            const std::uint32_t bit = 15U - pixel;
            const std::uint32_t index =
                ((data_a >> bit) & 0x01U) | (((data_b >> bit) & 0x01U) << 1U);
            if (index == 0U) {
                continue;
            }
            const std::uint16_t shift = static_cast<std::uint16_t>(sprite * 2U);
            const std::uint16_t mask = static_cast<std::uint16_t>(0x0003U << shift);
            const std::int32_t target_x = (x + static_cast<std::int32_t>(pixel)) * pixel_scale;
            for (std::int32_t subpixel = 0; subpixel < pixel_scale; ++subpixel) {
                const std::int32_t scaled_x = target_x + subpixel;
                if (scaled_x < 0 || scaled_x >= static_cast<std::int32_t>(width)) {
                    continue;
                }
                const std::size_t out = static_cast<std::size_t>(visible_line) * stride +
                                        static_cast<std::size_t>(scaled_x);
                sprite_sample_[out] = static_cast<std::uint16_t>(
                    (sprite_sample_[out] & ~mask) | (static_cast<std::uint16_t>(index) << shift));
            }
        }
    }

    bool agnus::playfield_blocks_sprite(std::uint8_t playfields,
                                        std::uint32_t sprite_group) const noexcept {
        const std::uint8_t group = static_cast<std::uint8_t>(sprite_group & 0x03U);
        const std::uint8_t pf1_priority = static_cast<std::uint8_t>(bplcon2_ & 0x0007U);
        const std::uint8_t pf2_priority = static_cast<std::uint8_t>((bplcon2_ >> 3U) & 0x0007U);
        const bool pf1_blocks = (playfields & pf1_sample) != 0U && pf1_priority <= group;
        const bool pf2_blocks = (playfields & pf2_sample) != 0U && pf2_priority <= group;
        return pf1_blocks || pf2_blocks;
    }

    bool agnus::collision_bitplane_match(std::uint8_t plane_bits) const noexcept {
        const std::uint8_t enable = static_cast<std::uint8_t>((clxcon_ >> 6U) & 0x003FU);
        if (enable == 0U) {
            return true;
        }
        const std::uint8_t match = static_cast<std::uint8_t>(clxcon_ & 0x003FU);
        return (((plane_bits ^ match) & enable) == 0U);
    }

    std::uint8_t agnus::collision_sprite_groups(std::uint16_t sprites) const noexcept {
        std::uint8_t groups = 0U;
        for (std::uint32_t group = 0U; group < max_sprites / 2U; ++group) {
            const std::uint32_t even_sprite = group * 2U;
            const std::uint32_t odd_sprite = even_sprite + 1U;
            const bool even_hit = sprite_value(sprites, even_sprite) != 0U;
            const bool odd_enabled = (clxcon_ & (0x1000U << group)) != 0U;
            const bool odd_hit = odd_enabled && sprite_value(sprites, odd_sprite) != 0U;
            if (even_hit || odd_hit) {
                groups = static_cast<std::uint8_t>(groups | (1U << group));
            }
        }
        return groups;
    }

    void agnus::latch_collisions(std::uint8_t sprite_groups, std::uint8_t plane_bits) noexcept {
        for (std::uint32_t a = 0U; a < max_sprites / 2U; ++a) {
            if ((sprite_groups & (1U << a)) == 0U) {
                continue;
            }
            for (std::uint32_t b = a + 1U; b < max_sprites / 2U; ++b) {
                if ((sprite_groups & (1U << b)) == 0U) {
                    continue;
                }
                const std::uint8_t bit = sprite_group_collision_bit(a, b);
                clxdat_ = static_cast<std::uint16_t>(clxdat_ | (1U << bit));
            }
        }

        if (!collision_bitplane_match(plane_bits)) {
            return;
        }
        constexpr std::uint8_t odd_bitplanes = 0x15U;  // BPL1, BPL3, BPL5
        constexpr std::uint8_t even_bitplanes = 0x2AU; // BPL2, BPL4, BPL6
        const bool odd_hit = (plane_bits & odd_bitplanes) != 0U;
        const bool even_hit = (plane_bits & even_bitplanes) != 0U;
        if (odd_hit && even_hit) {
            clxdat_ = static_cast<std::uint16_t>(clxdat_ | 0x0001U);
        }
        for (std::uint32_t group = 0U; group < max_sprites / 2U; ++group) {
            if ((sprite_groups & (1U << group)) == 0U) {
                continue;
            }
            if (odd_hit) {
                clxdat_ = static_cast<std::uint16_t>(clxdat_ | (1U << (1U + group)));
            }
            if (even_hit) {
                clxdat_ = static_cast<std::uint16_t>(clxdat_ | (1U << (5U + group)));
            }
        }
    }

    frame_buffer_view agnus::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = active_width(),
                .height = active_height(),
                .stride = framebuffer_stride};
    }

    // --- state -------------------------------------------------------------

    void agnus::save_state(state_writer& writer) const {
        writer.u16(dmacon_);
        writer.boolean(blitter_busy_);
        writer.boolean(blitter_zero_);
        writer.boolean(is_pal_);
        writer.u32(scanline_);
        writer.u32(color_clock_);
        writer.boolean(long_frame_);
        writer.u64(frame_index_);
        for (std::uint32_t p : bitplane_pointer_) {
            writer.u32(p);
        }
        for (const sprite_state& s : sprite_) {
            writer.u32(s.pointer);
            writer.u16(s.pos);
            writer.u16(s.ctl);
            writer.u16(s.data_a);
            writer.u16(s.data_b);
            writer.boolean(s.armed);
        }
        writer.u16(static_cast<std::uint16_t>(modulo_even_));
        writer.u16(static_cast<std::uint16_t>(modulo_odd_));
        writer.u16(bplcon0_);
        writer.u16(bplcon1_);
        writer.u16(bplcon2_);
        writer.u16(clxcon_);
        writer.u16(clxdat_);
        writer.u16(diwstrt_);
        writer.u16(diwstop_);
        writer.u16(ddfstrt_);
        writer.u16(ddfstop_);
        writer.u32(cop1lc_);
        writer.u32(cop2lc_);
        writer.u32(copper_pc_);
        writer.boolean(copper_running_);
        writer.boolean(copper_danger_);
        writer.u8(copper_delay_);
    }

    void agnus::load_state(state_reader& reader) {
        dmacon_ = reader.u16();
        blitter_busy_ = reader.boolean();
        blitter_zero_ = reader.boolean();
        is_pal_ = reader.boolean();
        scanline_ = reader.u32();
        color_clock_ = reader.u32();
        long_frame_ = reader.boolean();
        frame_index_ = reader.u64();
        for (std::uint32_t& p : bitplane_pointer_) {
            p = reader.u32() & chip_address_mask;
        }
        for (sprite_state& s : sprite_) {
            s.pointer = reader.u32() & chip_address_mask;
            s.pos = reader.u16();
            s.ctl = reader.u16();
            s.data_a = reader.u16();
            s.data_b = reader.u16();
            s.armed = reader.boolean();
        }
        modulo_even_ = static_cast<std::int16_t>(reader.u16());
        modulo_odd_ = static_cast<std::int16_t>(reader.u16());
        bplcon0_ = reader.u16();
        bplcon1_ = reader.u16();
        bplcon2_ = reader.u16();
        clxcon_ = reader.u16();
        clxdat_ = reader.u16();
        diwstrt_ = reader.u16();
        diwstop_ = reader.u16();
        ddfstrt_ = reader.u16();
        ddfstop_ = reader.u16();
        cop1lc_ = reader.u32() & copper_address_mask_;
        cop2lc_ = reader.u32() & copper_address_mask_;
        copper_pc_ = reader.u32() & copper_address_mask_;
        copper_running_ = reader.boolean();
        copper_danger_ = reader.boolean();
        copper_delay_ = reader.u8();
        refresh_blank_flags();
    }

    instrumentation::ichip_introspection& agnus::introspection() noexcept { return introspection_; }

    frame_buffer_view agnus::introspection_surface::palette_layer::view() const {
        // Decode the 32 palette entries as a 16-per-row swatch grid, each
        // entry an 8x8 solid block. Rebuilt on each call.
        const agnus& a = *owner_;
        constexpr std::uint32_t per_row = 16U;
        constexpr std::uint32_t cell = 8U;
        constexpr std::uint32_t sheet_width = per_row * cell;
        constexpr std::uint32_t sheet_height = (palette_entries / per_row) * cell;
        a.palette_sheet_.assign(static_cast<std::size_t>(sheet_width) * sheet_height, 0U);

        for (std::uint32_t entry = 0; entry < palette_entries; ++entry) {
            const std::uint32_t rgb = color_to_rgb(a.palette_word(entry));
            const std::uint32_t base_x = (entry % per_row) * cell;
            const std::uint32_t base_y = (entry / per_row) * cell;
            for (std::uint32_t cy = 0; cy < cell; ++cy) {
                for (std::uint32_t cx = 0; cx < cell; ++cx) {
                    a.palette_sheet_[static_cast<std::size_t>(base_y + cy) * sheet_width + base_x +
                                     cx] = rgb;
                }
            }
        }
        return {.pixels = a.palette_sheet_.data(),
                .width = sheet_width,
                .height = sheet_height,
                .stride = 0U};
    }

    namespace {
        [[maybe_unused]] const auto agnus_registration =
            register_factory("commodore.agnus", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<agnus>(); });
    } // namespace

} // namespace mnemos::chips::video
