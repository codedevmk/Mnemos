#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Commodore Denise (8362) video shifter (Amiga OCS/ECS display chip).
    //
    // Ported from the Emu reference (chips/denise); clean-room per public
    // Commodore Denise (8362) documentation. The reference modelled the
    // register surface only -- the 32-entry 12-bit colour palette, the
    // BPLCON0-3 mode-control registers (with a decoded BPLCON0 view), the
    // six BPLxDAT bitplane latches, and the 4-bit-to-8-bit DAC colour
    // expansion. This chip ports that surface integer-exact and adds the
    // documented bitplane-to-pixel serializer over a board-attached frame
    // so it can drive an ivideo framebuffer.
    //
    // Denise serializes up to six planar bitplanes through a colour lookup.
    // For a given pixel the same-column bit of each enabled plane is
    // gathered MSB-first into a colour index (plane 0 = bit 0); that index
    // selects one of 32 12-bit colour registers, expanded to 0x00RRGGBB by
    // replicating each 4-bit channel into 8 bits (the hardware DAC rule
    // 0xF -> 0xFF, 0x5 -> 0x55). Beyond the simple n-plane lookup Denise
    // offers two OCS extra-colour modes selected by BPLCON0:
    //   * EHB (Extra-Half-Brite): six planes, not HAM/dual-playfield. Index
    //     bit 5 halves the brightness of the colour selected by bits 4:0.
    //   * HAM (Hold-And-Modify): six planes. Bits 5:4 are a command; bits
    //     3:0 are data. Command 0 = load colour from the register file
    //     (bits 3:0 index colours 0-15); 1/2/3 = hold the previous pixel
    //     and replace its B / R / G channel with the 4-bit data (the held
    //     channel's low nibble is replicated to 8 bits).
    //   * Dual-playfield: odd planes (0,2,4) form playfield 1, even planes
    //     (1,3,5) playfield 2; a transparent (index 0) front playfield
    //     pixel shows the back, whose colours live at register +8.
    //
    // The board attaches non-owning bitplane spans (one packed 1bpp bitmap
    // per plane, row-major, `plane_stride_bytes` bytes per row, MSB the
    // leftmost pixel) plus the active plane count, resolution, and display
    // window. tick(cycles) advances one pixel per cycle across a 455x312
    // raster (lores); entering vblank renders the completed frame and bumps
    // frame_index(). The scanline callback fires at the start of every line.
    class denise final : public ivideo {
      public:
        // OCS lores visible raster (a PAL-ish 320x256 display window inside
        // a 455x312 total beam). Hires doubles the horizontal sample rate
        // into the same visible width; the geometry stays fixed so the
        // framebuffer view is stable.
        static constexpr std::uint32_t visible_width = 320U;
        static constexpr std::uint32_t visible_height = 256U;
        static constexpr std::uint32_t line_pixels = 455U; // total per line (lores)
        static constexpr std::uint32_t frame_lines = 312U; // total per frame
        static constexpr std::size_t bitplane_count = 6U;  // BPL1..BPL6
        static constexpr std::size_t palette_size = 32U;   // COLOR00..COLOR31
        static constexpr std::uint16_t color_word_mask = 0x0FFFU;

        // BPLCON0 bit layout (mirrors the hardware register positions).
        static constexpr std::uint16_t bplcon0_hires = 0x8000U;
        static constexpr std::uint16_t bplcon0_bpu_mask = 0x7000U;
        static constexpr std::uint16_t bplcon0_bpu_shift = 12U;
        static constexpr std::uint16_t bplcon0_ham = 0x0800U;
        static constexpr std::uint16_t bplcon0_dpf = 0x0400U;
        static constexpr std::uint16_t bplcon0_color = 0x0200U;
        static constexpr std::uint16_t bplcon0_gaud = 0x0100U;
        static constexpr std::uint16_t bplcon0_lpen = 0x0008U;
        static constexpr std::uint16_t bplcon0_lace = 0x0004U;
        static constexpr std::uint16_t bplcon0_ersy = 0x0002U;
        static constexpr std::uint16_t bplcon0_ecsena = 0x0001U;

        using line_callback = std::function<void(std::uint32_t line)>;

        denise() { reset(reset_kind::power_on); }

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

        // Board attachment (non-owning; the board's bitplane RAM outlives
        // the chip). Plane `n` is a packed 1bpp bitmap, `plane_stride_bytes`
        // bytes per scanline, MSB = leftmost pixel.
        void attach_bitplane(std::size_t plane, std::span<const std::uint8_t> bits) noexcept {
            if (plane < bitplane_count) {
                planes_[plane] = bits;
            }
        }
        void set_plane_stride(std::uint32_t bytes) noexcept { plane_stride_bytes_ = bytes; }

        // Colour register writes (COLOR00..COLOR31). Stores the 12-bit RGB
        // word; bits above 11:0 are dropped, matching the hardware cell.
        void write_color(std::size_t index, std::uint16_t value) noexcept {
            if (index < palette_size) {
                palette_[index] = value & color_word_mask;
            }
        }
        [[nodiscard]] std::uint16_t read_color(std::size_t index) const noexcept {
            return index < palette_size ? palette_[index] : 0U;
        }
        // 12-bit colour register -> 0x00RRGGBB via the 4-bit DAC expansion.
        [[nodiscard]] std::uint32_t palette_rgb888(std::size_t index) const noexcept;

        // Mode-control register writes. BPLCON0 refreshes the decoded view.
        void write_bplcon0(std::uint16_t value) noexcept;
        void write_bplcon1(std::uint16_t value) noexcept { bplcon1_ = value; }
        void write_bplcon2(std::uint16_t value) noexcept { bplcon2_ = value; }
        void write_bplcon3(std::uint16_t value) noexcept { bplcon3_ = value; }
        [[nodiscard]] std::uint16_t read_bplcon0() const noexcept { return bplcon0_; }

        // Decoded BPLCON0 view (refreshed on every BPLCON0 write).
        struct bplcon0_decoded final {
            std::uint8_t bitplane_count{}; // BPU field (0..6)
            bool hires{};
            bool ham{};
            bool dual_playfield{};
            bool color_enable{};
            bool genlock_audio{};
            bool light_pen{};
            bool interlace{};
            bool external_resync{};
            bool ecs_enabled{};
        };
        [[nodiscard]] const bplcon0_decoded& decoded() const noexcept { return decoded_; }

        // Display disable (board control bit): blanked frames render black.
        void set_display_enable(bool enabled) noexcept { display_enabled_ = enabled; }

        // Fired at the start of every line with its number.
        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        // Fired at the start of the first vblank line (after the frame renders).
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        // Beam position, for tests and debugging.
        [[nodiscard]] std::uint32_t beam_line() const noexcept { return beam_y_; }
        [[nodiscard]] std::uint32_t beam_dot() const noexcept { return beam_x_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(denise& owner) noexcept : palette_(owner) {}
            [[nodiscard]] std::span<instrumentation::debug_layer* const> debug_layers() override {
                layer_ptr_[0] = &palette_;
                return layer_ptr_;
            }

          private:
            class palette_layer final : public instrumentation::debug_layer {
              public:
                explicit palette_layer(denise& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::string_view name() const noexcept override { return "palette"; }
                [[nodiscard]] frame_buffer_view view() const override;

              private:
                denise* owner_;
            };

            palette_layer palette_;
            std::array<instrumentation::debug_layer*, 1> layer_ptr_{};
        };

        void render_frame() noexcept;
        // Gather the colour index from the enabled planes at the visible
        // (x, y). Returns the raw 0..63 plane index (caller applies the
        // colour mode).
        [[nodiscard]] std::uint32_t plane_index_at(std::uint32_t x, std::uint32_t y) const noexcept;
        [[nodiscard]] std::uint32_t plane_bit_at(std::size_t plane, std::uint32_t x,
                                                 std::uint32_t y) const noexcept;
        // Expand a 4-bit channel to 8 bits by nibble replication.
        [[nodiscard]] static constexpr std::uint8_t expand4(std::uint8_t nibble) noexcept {
            const std::uint8_t v = nibble & 0x0FU;
            return static_cast<std::uint8_t>((v << 4U) | v);
        }

        std::vector<std::uint32_t> pixels_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(visible_width) * visible_height);
        // The decoded palette swatch debug view, rebuilt on demand.
        mutable std::vector<std::uint32_t> palette_sheet_;

        std::array<std::span<const std::uint8_t>, bitplane_count> planes_{};
        std::uint32_t plane_stride_bytes_{(visible_width + 7U) / 8U};

        std::array<std::uint16_t, palette_size> palette_{};
        std::uint16_t bplcon0_{};
        std::uint16_t bplcon1_{};
        std::uint16_t bplcon2_{};
        std::uint16_t bplcon3_{};
        bplcon0_decoded decoded_{};

        bool display_enabled_{true};

        std::uint32_t beam_x_{};
        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        line_callback vblank_cb_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
