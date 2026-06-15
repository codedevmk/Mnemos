#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Sega VDP2 -- the Saturn's tile-based scrolling-background and colour-math
    // processor. On real hardware it composites four normal scroll planes
    // (NBG0..NBG3), two rotation planes (RBG0/RBG1), and the VDP1 sprite layer,
    // applying per-layer windows, colour calculation, line/back colour, mosaic,
    // and colour offset before emitting the final signal.
    //
    // This model is a deliberately PARTIAL port of the much larger hardware
    // (provenance: the Emu vdp2 C core; see NOTES.md). The compiling subset
    // covers:
    //   * The 256 word-aligned 16-bit register file with read-only TVSTAT /
    //     HCNT / VCNT handling and decode of TVMD (display / resolution /
    //     interlace), BGON (per-plane enable + transparent-code), and RAMCTL
    //     (CRAM colour mode 0..2).
    //   * 512 KiB VRAM + 4 KiB colour RAM (CRAM), with palette lookup across
    //     all three CRAM modes returning packed 0x00RRGGBB.
    //   * Per-scanline back-colour injection.
    //   * The NBG normal-scroll TILEMAP fetch + a priority-ordered NBG
    //     compositor (NBG0..NBG3, 4bpp / 8bpp / 16bpp cells, 1-word and 2-word
    //     pattern names, plane sizes, integer scroll, character flip).
    //
    // NOT modelled here (deferred): rotation layers (RBG0/RBG1) and the whole
    // rotation-matrix / coefficient-table machinery, bitmap-mode NBGs, the VDP1
    // sprite layer, window masks, colour calculation / blending, mosaic, line
    // colour, line scroll, vertical cell scroll, zoom, and colour offset. Those
    // register families are still stored and read back faithfully; they simply
    // do not yet drive the compositor.
    //
    // The chip exposes its primary composited frame through ivideo. tick()
    // advances a simple beam over the active raster and renders the completed
    // frame when the beam re-enters vblank, bumping frame_index().
    class vdp2 final : public ivideo {
      public:
        static constexpr std::size_t vram_size = 512U * 1024U;
        static constexpr std::size_t cram_size = 4U * 1024U;
        static constexpr std::size_t reg_count = 256U; // 256 16-bit registers

        // Legacy fixed render geometry (NTSC low-res); the active TVMD-driven
        // width/height clamp to these in this subset.
        static constexpr std::uint32_t render_width = 320U;
        static constexpr std::uint32_t render_height = 224U;
        static constexpr std::uint32_t total_lines = 263U; // NTSC frame lines

        // Register byte offsets (within the 0..0x1FE word-aligned range).
        static constexpr std::uint16_t reg_tvmd = 0x00U;
        static constexpr std::uint16_t reg_tvstat = 0x04U;
        static constexpr std::uint16_t reg_hcnt = 0x08U;
        static constexpr std::uint16_t reg_vcnt = 0x0AU;
        static constexpr std::uint16_t reg_ramctl = 0x0EU;
        static constexpr std::uint16_t reg_bgon = 0x20U;
        static constexpr std::uint16_t reg_chctla = 0x28U;
        static constexpr std::uint16_t reg_chctlb = 0x2AU;
        static constexpr std::uint16_t reg_plsz = 0x3AU;
        static constexpr std::uint16_t reg_mpofn = 0x3CU;
        static constexpr std::uint16_t reg_mpabn0 = 0x40U;
        static constexpr std::uint16_t reg_pncn0 = 0x30U;
        static constexpr std::uint16_t reg_scxin0 = 0x70U;
        static constexpr std::uint16_t reg_scyin0 = 0x74U;
        static constexpr std::uint16_t reg_scxin1 = 0x80U;
        static constexpr std::uint16_t reg_scyin1 = 0x84U;
        static constexpr std::uint16_t reg_scxn2 = 0x90U;
        static constexpr std::uint16_t reg_scyn2 = 0x92U;
        static constexpr std::uint16_t reg_scxn3 = 0x94U;
        static constexpr std::uint16_t reg_scyn3 = 0x96U;
        static constexpr std::uint16_t reg_bktau = 0xACU;
        static constexpr std::uint16_t reg_bktal = 0xAEU;
        static constexpr std::uint16_t reg_craofa = 0xE4U;
        static constexpr std::uint16_t reg_prina = 0xF8U;
        static constexpr std::uint16_t reg_prinb = 0xFAU;

        vdp2() { reset(reset_kind::power_on); }

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

        // Register access (reg is the byte offset; reads/writes are 16-bit
        // word-aligned). Read-only registers drop writes.
        [[nodiscard]] std::uint16_t reg_read(std::uint16_t reg) const noexcept;
        void reg_write(std::uint16_t reg, std::uint16_t value) noexcept;

        // Direct VRAM / CRAM byte access for the board to populate.
        [[nodiscard]] std::span<std::uint8_t> vram() noexcept { return vram_; }
        [[nodiscard]] std::span<std::uint8_t> cram() noexcept { return cram_; }

        // Host latches for the read-only TVSTAT fields and beam counters.
        void set_vblank(bool in_vblank) noexcept { vblank_ = in_vblank; }
        void set_hblank(bool in_hblank) noexcept { hblank_ = in_hblank; }
        void set_field(bool odd_field) noexcept { field_odd_ = odd_field; }
        void set_pal(bool pal) noexcept { is_pal_ = pal; }

        // Decoded TVMD state.
        [[nodiscard]] bool display_on() const noexcept { return display_on_; }
        [[nodiscard]] std::uint8_t hres() const noexcept { return hres_; }
        [[nodiscard]] std::uint8_t vres() const noexcept { return vres_; }
        [[nodiscard]] std::uint8_t cram_mode() const noexcept { return cram_mode_; }
        [[nodiscard]] std::uint32_t display_width() const noexcept;
        [[nodiscard]] std::uint32_t display_height() const noexcept;

        // Look up a CRAM palette entry by raw index; returns packed 0x00RRGGBB.
        [[nodiscard]] std::uint32_t palette_read(std::uint16_t index) const noexcept;
        // Back-colour for the given scanline; returns packed 0x00RRGGBB.
        [[nodiscard]] std::uint32_t back_color(int scanline) const noexcept;

      private:
        // Decoded per-NBG tile-layer configuration (subset of the hardware).
        struct nbg_cfg final {
            bool enabled{};
            bool transparent_code_enabled{true};
            std::uint8_t priority{};     // 0..7 (0 = not displayed)
            std::uint8_t char_size{};    // 0 = 8x8, 1 = 16x16
            std::uint8_t pattern_bits{}; // 0 = 4bpp, 1 = 8bpp, 2 = 16bpp
            std::uint8_t pattern_name{}; // 0 = 2-word, 1 = 1-word
            std::uint8_t plane_size{};   // 0 = 1x1, 1 = 2x1, 3 = 2x2
            std::array<std::uint32_t, 4> map_base{};
            std::int32_t scroll_x{};
            std::int32_t scroll_y{};
            std::uint16_t pal_bank{};
            std::uint16_t cram_offset{};
            std::uint16_t supplement_data{};
            std::uint8_t aux_mode{};
        };

        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(vdp2& owner) noexcept
                : vram_view_("vram", owner.vram_), cram_view_("cram", owner.cram_) {
                views_[0] = &vram_view_;
                views_[1] = &cram_view_;
            }
            [[nodiscard]] std::span<instrumentation::memory_view* const> memory_views() override {
                return views_;
            }

          private:
            instrumentation::span_memory_view vram_view_;
            instrumentation::span_memory_view cram_view_;
            std::array<instrumentation::memory_view*, 2> views_{};
        };

        [[nodiscard]] std::uint16_t vram_read_word(std::uint32_t off) const noexcept;
        [[nodiscard]] std::uint8_t vram_read_byte(std::uint32_t off) const noexcept;

        void decode_tvmd() noexcept;
        void decode_bgon() noexcept;
        void decode_ramctl() noexcept;
        void decode_nbg_cfg(int n, nbg_cfg& cfg) const noexcept;

        // Tilemap fetch: returns a packed 0x00RRGGBB pixel for NBG `n` at
        // screen (x, y), or 0 with the transparent flag set when nothing draws.
        [[nodiscard]] std::uint32_t nbg_fetch_pixel(const nbg_cfg& cfg, int x, int y,
                                                    bool& transparent) const noexcept;

        void render_scanline(int scanline) noexcept;
        void render_frame() noexcept;

        // Heap-backed: 512 KB as an in-object array would overflow the default
        // stack when two chips are live at once (e.g. a save/load round-trip).
        std::vector<std::uint8_t> vram_ = std::vector<std::uint8_t>(vram_size, 0U);
        std::array<std::uint8_t, cram_size> cram_{};
        std::array<std::uint16_t, reg_count> regs_{};

        std::vector<std::uint32_t> pixels_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(render_width) * render_height);

        bool display_on_{};
        std::uint8_t hres_{};
        std::uint8_t vres_{};
        std::uint8_t interlace_{};
        std::array<bool, 4> nbg_on_{};
        std::array<bool, 4> nbg_transparent_code_enabled_{true, true, true, true};
        std::uint8_t cram_mode_{};

        bool vblank_{};
        bool hblank_{};
        bool field_odd_{true};
        bool is_pal_{};

        std::uint32_t beam_y_{};
        std::uint64_t frame_index_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
