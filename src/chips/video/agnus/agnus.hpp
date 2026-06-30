#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace mnemos::chips::video {

    // Commodore Agnus (8367) — the home-computer chipset's central address
    // generator and display-DMA master, modelled here as the display
    // sequencer that drives the visible raster.
    //
    // Ported from the Emu reference (chips/agnus); clean-room per public
    // Commodore Agnus (8367) documentation. The reference Agnus is a pure
    // DMA scheduler + beam tracker (DMACON, beam position, VPOSR/VHPOSR,
    // per-channel DMA predicates) and the colour decode lives in the
    // companion display chip; this chip folds in the standard planar
    // bitplane fetch and that colour decode so it can satisfy the ivideo
    // framebuffer contract.
    //
    // Responsibilities:
    //   * Master DMA scheduler — DMACON with set/clear bit-15 semantics, the
    //     master enable, and the per-channel enables (bitplane, copper,
    //     blitter, sprite, disk, four audio channels).
    //   * Beam-position generator — a color-clock + scanline counter
    //     (PAL/NTSC selectable) feeding VPOSR / VHPOSR readback and the
    //     vertical/horizontal blank windows.
    //   * Copper co-processor — the WAIT / SKIP / MOVE list walker that
    //     pokes the display registers at programmed beam positions.
    //   * Display sequencer — as each scanline completes it walks the active
    //     bitplane pointers across the attached chip RAM and decodes the
    //     planar pixels through the 32-entry 12-bit colour palette into a
    //     0x00RRGGBB framebuffer, including OCS HAM, EHB, and
    //     dual-playfield colour modes.
    //
    // The chip attaches non-owning spans for chip RAM (where bitplane data
    // and the copper list live) and the colour palette. Formats per the real
    // hardware:
    //   * Chip RAM is big-endian 16-bit words. Bitplane DMA fetches one
    //     16-bit word per plane per 16-pixel serializer cell, advancing
    //     each plane's pointer by 2 bytes per fetch and adding its modulo at
    //     end of line. Plane P contributes bit P of each pixel value
    //     (MSB = leftmost pixel of the word).
    //   * Palette is 32 entries of 16-bit big-endian colour words; bits 11:0
    //     hold the 4-bit-per-channel R:G:B triplet. Each 4-bit channel v
    //     expands to 8 bits as (v << 4) | v.
    //   * The display window (DIWSTRT / DIWSTOP) clips the visible region;
    //     the data-fetch window (DDFSTRT / DDFSTOP) bounds the per-line
    //     bitplane fetch. Colour index 0 is the background everywhere.
    //
    // tick(cycles) advances the beam one color clock per cycle; wrapping back
    // to scanline 0 finishes the completed frame, bumps frame_index(), and
    // fires the vertical-blank callback. The scanline callback fires at the
    // start of every line with its number; the board derives scanline-paced
    // glue from it. The Copper runs from the attached chip RAM during the
    // visible field.
    class agnus final : public ivideo {
      public:
        // Raster geometry, in color clocks (7.09 MHz ticks). PAL lines round
        // to 227 clocks; the long-line/short-line half-clock jitter is a
        // later refinement.
        static constexpr std::uint32_t color_clocks_per_line = 227U;
        static constexpr std::uint32_t scanlines_pal = 312U;
        static constexpr std::uint32_t scanlines_ntsc = 262U;

        // Visible raster: 320x256 low-resolution PAL (320x200 NTSC), or
        // 640x256/200 when BPLCON0.HIRES is set. The framebuffer stores the
        // worst-case 640-wide PAL backing and reports the active view width
        // per frame mode.
        static constexpr std::uint32_t visible_width_lores = 320U;
        static constexpr std::uint32_t visible_width_hires = 640U;
        static constexpr std::uint32_t visible_width = visible_width_lores;
        static constexpr std::uint32_t framebuffer_stride = visible_width_hires;
        static constexpr std::uint32_t visible_height_pal = 256U;
        static constexpr std::uint32_t visible_height_ntsc = 200U;

        static constexpr std::uint32_t max_bitplanes = 6U;
        static constexpr std::uint32_t max_hires_bitplanes = 4U;
        static constexpr std::uint32_t max_sprites = 8U;
        static constexpr std::uint32_t sprite_width = 16U;
        static constexpr std::uint32_t palette_entries = 32U;
        static constexpr std::uint32_t ocs_copper_address_mask = 0x0007FFFEU;
        static constexpr std::uint32_t ecs_1m_copper_address_mask = 0x000FFFFEU;

        // The visible field begins this color clock into the line and this
        // scanline down the frame (the standard OCS hardware origin). The
        // display-window registers are programmed against these origins.
        static constexpr std::uint32_t display_clock_origin = 0x81U / 2U; // DIWSTRT.H default
        static constexpr std::uint32_t display_line_origin = 0x2CU;       // DIWSTRT.V default

        // DMACON bit positions. Bit 15 on write is the SET/CLR selector.
        static constexpr std::uint16_t dmacon_set = 1U << 15U;
        static constexpr std::uint16_t dmacon_bbusy = 1U << 14U;
        static constexpr std::uint16_t dmacon_bzero = 1U << 13U;
        static constexpr std::uint16_t dmacon_bltpri = 1U << 10U;
        static constexpr std::uint16_t dmacon_dmaen = 1U << 9U;
        static constexpr std::uint16_t dmacon_bplen = 1U << 8U;
        static constexpr std::uint16_t dmacon_copen = 1U << 7U;
        static constexpr std::uint16_t dmacon_blten = 1U << 6U;
        static constexpr std::uint16_t dmacon_spren = 1U << 5U;
        static constexpr std::uint16_t dmacon_dsken = 1U << 4U;
        static constexpr std::uint16_t dmacon_aud3en = 1U << 3U;
        static constexpr std::uint16_t dmacon_aud2en = 1U << 2U;
        static constexpr std::uint16_t dmacon_aud1en = 1U << 1U;
        static constexpr std::uint16_t dmacon_aud0en = 1U << 0U;
        // Writable bits; 14:13 are read-only (blitter status) and 15 is the
        // SET/CLR selector.
        static constexpr std::uint16_t dmacon_writable = 0x07FFU;

        using line_callback = std::function<void(std::uint32_t line)>;
        using cycle_callback = std::function<void()>;
        using custom_write_callback = std::function<void(std::uint16_t reg, std::uint16_t value)>;

        agnus() { reset(reset_kind::power_on); }

        // ichip
        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // ivideo
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // Board attachment (non-owning; the board's RAM outlives the chip).
        // Chip RAM is the big-endian word memory bitplane DMA and the copper
        // list read from. The palette is 32 big-endian 12-bit colour words.
        void attach_chip_ram(std::span<const std::uint8_t> ram) noexcept { chip_ram_ = ram; }
        void attach_palette(std::span<const std::uint8_t> palette) noexcept { palette_ = palette; }

        // Region selection. PAL or NTSC changes the total scanline count,
        // the vertical-blank window, and the active visible height.
        void set_pal(bool is_pal) noexcept;
        [[nodiscard]] bool is_pal() const noexcept { return is_pal_; }

        // DMACON write with bit-15 set/clear semantics. Read-only bits
        // (BBUSY, BZERO) are ignored on write.
        void write_dmacon(std::uint16_t value) noexcept;
        // DMACONR readback: writable mask plus live BBUSY/BZERO bits.
        [[nodiscard]] std::uint16_t read_dmaconr() const noexcept;

        // VPOSR readback: bit 15 = LOF; bit 0 = high bit of the scanline.
        [[nodiscard]] std::uint16_t read_vposr() const noexcept;
        // VHPOSR readback: bits 15:8 = low byte of scanline; 7:0 = color clock.
        [[nodiscard]] std::uint16_t read_vhposr() const noexcept;

        // Master + per-channel DMA predicates (master AND the channel's bit).
        [[nodiscard]] bool dma_master() const noexcept;
        [[nodiscard]] bool dma_bitplane() const noexcept;
        [[nodiscard]] bool dma_copper() const noexcept;
        [[nodiscard]] bool dma_blitter() const noexcept;
        [[nodiscard]] bool dma_sprite() const noexcept;
        [[nodiscard]] bool dma_disk() const noexcept;
        [[nodiscard]] bool dma_audio(int channel) const noexcept;

        struct display_dma_wait final {
            std::uint32_t cycles{};
        };
        // Additional 68000 cycles before the CPU can use chip RAM while display
        // DMA owns the current open memory slot.
        [[nodiscard]] display_dma_wait
        display_dma_cpu_wait(std::uint32_t instruction_cycles_before_access) const noexcept;
        [[nodiscard]] std::uint32_t
        display_dma_cpu_wait_cycles(std::uint32_t instruction_cycles_before_access) const noexcept;
        [[nodiscard]] std::uint32_t
        sprite_dma_cpu_wait_cycles(std::uint32_t instruction_cycles_before_access) const noexcept;

        // Blitter status bits reflected into DMACONR (driven by the board's
        // blitter subsystem once it lands).
        void set_blitter_busy(bool busy) noexcept { blitter_busy_ = busy; }
        void set_blitter_zero(bool zero) noexcept { blitter_zero_ = zero; }

        // Display registers. Programmed by the board's custom-register
        // writes (or by the copper). Pointers are 20-bit word-aligned chip
        // RAM byte addresses; modulos are signed per-line byte adjustments.
        void set_bitplane_pointer(std::uint32_t plane, std::uint32_t byte_address) noexcept;
        void write_bitplane_pointer_word(std::uint32_t plane, bool high,
                                         std::uint16_t value) noexcept;
        [[nodiscard]] std::uint32_t bitplane_pointer(std::uint32_t plane) const noexcept;
        void set_bitplane_modulo_even(std::int16_t modulo) noexcept { modulo_even_ = modulo; }
        void set_bitplane_modulo_odd(std::int16_t modulo) noexcept { modulo_odd_ = modulo; }
        // BPLCON0: bit 15 = HIRES, bits 14:12 = bitplane count (BPU), bit
        // 11 = HAM, bit 10 = dual playfield. OCS high-resolution display
        // is limited to four bitplanes.
        void set_bplcon0(std::uint16_t value) noexcept { bplcon0_ = value; }
        // BPLCON1 delays odd and even playfield serializers independently:
        // bits 3:0 for playfield 1 (BPL1/3/5) and bits 7:4 for playfield 2
        // (BPL2/4/6). OCS uses 0..15 low-resolution pixels of delay; high
        // resolution shifts in two-pixel increments.
        void set_bplcon1(std::uint16_t value) noexcept { bplcon1_ = value & 0x00FFU; }
        // BPLCON2 controls playfield-vs-sprite priority. In normal playfield
        // mode the hardware uses the PF2 priority field for the merged playfield.
        void set_bplcon2(std::uint16_t value) noexcept { bplcon2_ = value & 0x007FU; }
        // CLXCON selects the bitplane/sprite collision sources; CLXDAT is a
        // read-to-clear latch.
        void write_clxcon(std::uint16_t value) noexcept { clxcon_ = value; }
        [[nodiscard]] std::uint16_t read_clxdat() noexcept;
        // Display window start/stop, raw register words (V in 8:8, H low).
        void set_diwstrt(std::uint16_t value) noexcept { diwstrt_ = value; }
        void set_diwstop(std::uint16_t value) noexcept { diwstop_ = value; }
        // Data-fetch window start/stop, in color clocks (bits 7:0).
        void set_ddfstrt(std::uint16_t value) noexcept { ddfstrt_ = value; }
        void set_ddfstop(std::uint16_t value) noexcept { ddfstop_ = value; }

        // Sprite registers. Pointer words are merged as 20-bit word-aligned
        // chip-RAM byte addresses. DATA writes arm the manual sprite output;
        // POS/CTL writes disarm it, matching the comparator reset rule.
        void set_sprite_pointer(std::uint32_t sprite, std::uint32_t byte_address) noexcept;
        [[nodiscard]] std::uint32_t sprite_pointer(std::uint32_t sprite) const noexcept;
        void write_sprite_pointer_word(std::uint32_t sprite, bool high,
                                       std::uint16_t value) noexcept;
        void write_sprite_pos(std::uint32_t sprite, std::uint16_t value) noexcept;
        void write_sprite_ctl(std::uint32_t sprite, std::uint16_t value) noexcept;
        void write_sprite_data_a(std::uint32_t sprite, std::uint16_t value) noexcept;
        void write_sprite_data_b(std::uint32_t sprite, std::uint16_t value) noexcept;

        // Copper list base pointers + control. COPJMP1/COPJMP2 reload the
        // live program counter. Copper MOVEs cannot target $000-$03e, while
        // COPCON bit 1 (DANGER) opens the protected $040-$07e blitter range;
        // $080+ custom registers are always writable. The copper writes
        // display registers back through this chip via apply_copper_move().
        void set_custom_write_callback(custom_write_callback cb) noexcept {
            custom_write_cb_ = std::move(cb);
        }
        void set_copper_address_mask(std::uint32_t mask) noexcept;
        void write_cop1lc(std::uint32_t value) noexcept;
        void write_cop2lc(std::uint32_t value) noexcept;
        [[nodiscard]] std::uint16_t dmacon() const noexcept { return dmacon_; }
        [[nodiscard]] std::uint32_t cop1lc() const noexcept { return cop1lc_; }
        [[nodiscard]] std::uint32_t cop2lc() const noexcept { return cop2lc_; }
        [[nodiscard]] std::uint32_t copper_pc() const noexcept { return copper_pc_; }
        [[nodiscard]] bool copper_running() const noexcept { return copper_running_; }
        [[nodiscard]] std::uint8_t copper_delay() const noexcept { return copper_delay_; }
        void write_copcon(std::uint16_t value) noexcept {
            copper_danger_ = (value & 0x0002U) != 0U;
        }
        void strobe_copjmp1() noexcept;
        void strobe_copjmp2() noexcept;

        // Fired at the start of every line with its number.
        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        // Fired once per color clock so board-level DMA users can retire
        // cycle-bounded work against the same beam clock as the Copper.
        void set_cycle_callback(cycle_callback cb) noexcept { cycle_cb_ = std::move(cb); }
        // Fired at the start of the first vertical-blank line (after the
        // frame renders); kept alongside the scanline callback for tooling.
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        // Beam position, for tests and debugging.
        [[nodiscard]] std::uint32_t beam_line() const noexcept { return scanline_; }
        [[nodiscard]] std::uint32_t beam_clock() const noexcept { return color_clock_; }
        [[nodiscard]] std::uint32_t active_height() const noexcept {
            return is_pal_ ? visible_height_pal : visible_height_ntsc;
        }
        [[nodiscard]] bool hires_enabled() const noexcept { return (bplcon0_ & 0x8000U) != 0U; }
        [[nodiscard]] std::uint32_t active_width() const noexcept {
            return hires_enabled() ? visible_width_hires : visible_width_lores;
        }

        // Convert a 12-bit colour word (0x0RGB) to 0x00RRGGBB. Ported
        // integer-exact from the reference colour decode (nibble replicate).
        [[nodiscard]] static std::uint32_t color_to_rgb(std::uint16_t color12) noexcept;

      private:
        struct display_dma_slot final {
            bool active{};
            bool hires{};
            std::uint32_t active_planes{};
            std::uint32_t group_offset{};
            std::uint32_t beam_clock{};
            std::uint32_t fetch_end{};
        };

        struct sprite_dma_slot final {
            bool active{};
            std::uint32_t sprite{};
            std::uint32_t beam_clock{};
        };

        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(agnus& owner) noexcept : palette_(owner) {}
            [[nodiscard]] std::span<instrumentation::debug_layer* const> debug_layers() override {
                layer_ptr_[0] = &palette_;
                return layer_ptr_;
            }

          private:
            class palette_layer final : public instrumentation::debug_layer {
              public:
                explicit palette_layer(agnus& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::string_view name() const noexcept override { return "palette"; }
                [[nodiscard]] frame_buffer_view view() const override;

              private:
                agnus* owner_;
            };

            palette_layer palette_;
            std::array<instrumentation::debug_layer*, 1> layer_ptr_{};
        };

        [[nodiscard]] std::uint16_t chip_word(std::uint32_t byte_address) const noexcept;
        [[nodiscard]] std::uint16_t palette_word(std::uint32_t index) const noexcept;
        [[nodiscard]] std::uint32_t bitplane_count() const noexcept;
        [[nodiscard]] display_dma_slot
        display_dma_slot_at(std::uint32_t instruction_cycles_before_access) const noexcept;
        [[nodiscard]] display_dma_slot
        display_dma_slot_at_beam(std::uint32_t beam_line, std::uint32_t beam_clock) const noexcept;
        [[nodiscard]] bool
        display_dma_owns_memory_slot(const display_dma_slot& slot) const noexcept;
        [[nodiscard]] bool display_dma_copper_blocked() const noexcept;
        [[nodiscard]] sprite_dma_slot
        sprite_dma_slot_at(std::uint32_t instruction_cycles_before_access) const noexcept;
        [[nodiscard]] bool display_dma_steals_sprite_word(std::uint32_t sprite,
                                                          std::uint32_t beam_line,
                                                          std::uint32_t word_index) const noexcept;
        [[nodiscard]] bool display_dma_steals_sprite_slot(std::uint32_t sprite,
                                                          std::uint32_t beam_line) const noexcept;
        [[nodiscard]] bool sprite_dma_fetch_requested(std::uint32_t sprite,
                                                      std::uint32_t beam_line) const noexcept;
        [[nodiscard]] bool sprite_dma_copper_blocked() const noexcept;
        void refresh_blank_flags() noexcept;
        void run_copper() noexcept;
        void apply_copper_move(std::uint16_t reg_addr, std::uint16_t value) noexcept;
        void begin_frame_render() noexcept;
        void render_scanline(std::uint32_t beam_line) noexcept;
        void render_sprites(std::uint32_t height) noexcept;
        void render_dma_sprite(std::uint32_t sprite) noexcept;
        void render_manual_sprite(std::uint32_t sprite) noexcept;
        void render_sprite_line(std::uint32_t sprite, std::int32_t x, std::uint32_t visible_line,
                                std::uint16_t data_a, std::uint16_t data_b) noexcept;
        [[nodiscard]] bool playfield_blocks_sprite(std::uint8_t playfields,
                                                   std::uint32_t sprite_group) const noexcept;
        [[nodiscard]] bool collision_bitplane_match(std::uint8_t plane_bits) const noexcept;
        [[nodiscard]] std::uint8_t
        collision_sprite_groups(std::uint16_t sprite_bits) const noexcept;
        void latch_collisions(std::uint8_t sprite_groups, std::uint8_t plane_bits) noexcept;

        struct sprite_state final {
            std::uint32_t pointer{};
            std::uint16_t pos{};
            std::uint16_t ctl{};
            std::uint16_t data_a{};
            std::uint16_t data_b{};
            bool armed{};
        };

        std::vector<std::uint32_t> pixels_ = std::vector<std::uint32_t>(
            static_cast<std::size_t>(framebuffer_stride) * visible_height_pal);
        std::vector<std::uint8_t> bitplane_sample_ = std::vector<std::uint8_t>(
            static_cast<std::size_t>(framebuffer_stride) * visible_height_pal);
        std::vector<std::uint8_t> playfield_sample_ = std::vector<std::uint8_t>(
            static_cast<std::size_t>(framebuffer_stride) * visible_height_pal);
        std::vector<std::uint16_t> sprite_sample_ = std::vector<std::uint16_t>(
            static_cast<std::size_t>(framebuffer_stride) * visible_height_pal);
        // The decoded palette swatch debug view, rebuilt on demand.
        mutable std::vector<std::uint32_t> palette_sheet_;

        std::span<const std::uint8_t> chip_ram_{};
        std::span<const std::uint8_t> palette_{};

        // DMA controller.
        std::uint16_t dmacon_{};
        bool blitter_busy_{};
        bool blitter_zero_{};

        // Beam generator.
        bool is_pal_{true};
        std::uint32_t scanline_{};
        std::uint32_t color_clock_{};
        bool long_frame_{};
        bool vblank_active_{};
        bool hblank_active_{};

        // Display registers.
        std::array<std::uint32_t, max_bitplanes> bitplane_pointer_{};
        std::int16_t modulo_even_{};
        std::int16_t modulo_odd_{};
        std::uint16_t bplcon0_{};
        std::uint16_t bplcon1_{};
        std::uint16_t bplcon2_{};
        std::uint16_t clxcon_{};
        std::uint16_t clxdat_{};
        std::uint16_t diwstrt_{};
        std::uint16_t diwstop_{};
        std::uint16_t ddfstrt_{};
        std::uint16_t ddfstop_{};

        std::array<sprite_state, max_sprites> sprite_{};

        // Copper.
        std::uint32_t cop1lc_{};
        std::uint32_t cop2lc_{};
        std::uint32_t copper_pc_{};
        std::uint32_t copper_address_mask_{ocs_copper_address_mask};
        bool copper_running_{};
        bool copper_danger_{};
        std::uint8_t copper_delay_{};
        custom_write_callback custom_write_cb_{};

        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        cycle_callback cycle_cb_{};
        line_callback vblank_cb_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
