#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Ricoh 2C02 PPU (the home-console picture processor): two pattern tables
    // of 8x8 2bpp tiles composited as a scrolling background plus up to 64
    // hardware sprites, through a 32-entry index palette into a fixed 64-colour
    // master palette, over a 256x240 visible raster on a 341x262 NTSC frame.
    //
    // The CPU programmes the chip through eight registers at $2000..$2007
    // (mirrored every 8 bytes to $3FFF). The internal "loopy" scroll state
    // (v/t/x/w) accumulates the coarse/fine scroll the same way the silicon
    // does; reg_write/reg_read are ported integer-exact from the reference.
    //
    // tick(cycles) advances one PPU dot per cycle across the frame; the
    // scanline callback fires at dot 0 of every line with its number, and the
    // frame renders at-once when the beam reaches the first post-render line
    // (the start of vblank), where frame_index() bumps and the vblank flag /
    // NMI gate latch. The framebuffer is 256x240 uint32 0x00RRGGBB.
    //
    // The board attaches non-owning spans for the chip's external memory (the
    // cartridge owns the CHR pattern data + the console owns CIRAM); the OAM
    // and palette RAM live inside the chip (the CPU fills them through the
    // register ports, exactly like the hardware). attach_* lets a test or the
    // board hand the chip backing memory directly.
    //
    //   * CHR pattern memory ($0000..$1FFF): 8x8 2bpp tiles, 16 bytes per tile
    //     -- 8 bytes of bitplane 0 (low bit) then 8 bytes of bitplane 1, MSB
    //     leftmost. A tile's pixel value (0..3) indexes one of eight 4-colour
    //     palettes.
    //   * Nametable CIRAM: 32x30 tile-code grid + a 64-byte attribute table per
    //     1 KB bank; mirroring (horizontal / vertical / single) folds the four
    //     logical $2000..$2FFF banks onto the two physical 1 KB banks.
    //   * Palette RAM (32 bytes): entries 0..15 background, 16..31 sprite; entry
    //     0 (and its mirrors at $04/$08/$0C) is the shared backdrop colour. Each
    //     byte is a 6-bit index into the 64-colour master palette.
    //   * OAM (256 bytes): 64 sprites of 4 bytes -- Y, tile code, attributes
    //     (palette 1:0, priority 5, flip-x 6, flip-y 7), X.
    //
    // Clean-room comment: ported from the Emu reference (chips/ppu2c02), clean-
    // room per public Ricoh 2C02 PPU documentation.
    class ppu2c02 final : public ivideo {
      public:
        static constexpr std::uint32_t visible_width = 256U;
        static constexpr std::uint32_t visible_height = 240U;
        static constexpr std::uint32_t dots_per_line = 341U;   // total per line (both regions)
        static constexpr std::uint32_t lines_per_frame = 262U; // NTSC total
        static constexpr std::uint32_t pal_lines_per_frame =
            312U; // PAL total (50 more vblank lines)
        // The first post-render line where vblank begins and the frame renders.
        static constexpr std::uint32_t vblank_line = 241U;

        static constexpr std::size_t oam_size = 256U;
        static constexpr std::size_t palette_size = 32U;
        static constexpr std::size_t nametable_size = 2048U; // 2 KB CIRAM
        static constexpr std::size_t pattern_size = 0x2000U; // 8 KB CHR window
        static constexpr std::size_t tile_bytes = 16U;       // one 8x8 2bpp tile

        // Register offsets within the $2000..$2007 window.
        static constexpr std::uint8_t reg_ctrl = 0U;
        static constexpr std::uint8_t reg_mask = 1U;
        static constexpr std::uint8_t reg_status = 2U;
        static constexpr std::uint8_t reg_oamaddr = 3U;
        static constexpr std::uint8_t reg_oamdata = 4U;
        static constexpr std::uint8_t reg_scroll = 5U;
        static constexpr std::uint8_t reg_addr = 6U;
        static constexpr std::uint8_t reg_data = 7U;

        // PPUCTRL bits.
        static constexpr std::uint8_t ctrl_vram_inc32 = 1U << 2U;
        static constexpr std::uint8_t ctrl_spr_pt = 1U << 3U;
        static constexpr std::uint8_t ctrl_bg_pt = 1U << 4U;
        static constexpr std::uint8_t ctrl_spr_size16 = 1U << 5U;
        static constexpr std::uint8_t ctrl_nmi_enable = 1U << 7U;

        // PPUMASK bits.
        static constexpr std::uint8_t mask_greyscale = 1U << 0U;
        static constexpr std::uint8_t mask_bg_left = 1U << 1U;
        static constexpr std::uint8_t mask_spr_left = 1U << 2U;
        static constexpr std::uint8_t mask_bg_enable = 1U << 3U;
        static constexpr std::uint8_t mask_spr_enable = 1U << 4U;

        // PPUSTATUS bits.
        static constexpr std::uint8_t status_spr_over = 1U << 5U;
        static constexpr std::uint8_t status_spr0_hit = 1U << 6U;
        static constexpr std::uint8_t status_vblank = 1U << 7U;

        // Nametable mirroring -- the cartridge's CIRAM A10 wiring folds the four
        // logical banks onto the two physical 1 KB banks (or single-screen).
        enum class mirroring : std::uint8_t {
            horizontal = 0U,
            vertical,
            single_a,
            single_b,
            four_screen,
        };

        using line_callback = std::function<void(std::uint32_t line)>;

        ppu2c02() { reset(reset_kind::power_on); }

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

        // Board attachment (non-owning; the cartridge / console RAM outlives the
        // chip). CHR is the cartridge pattern ROM/RAM; nametables is the
        // console's 2 KB CIRAM. Palette / OAM also have attach_* hooks so a test
        // can seed them directly (the CPU normally fills them via the ports).
        void attach_chr(std::span<const std::uint8_t> chr) noexcept { chr_ = chr; }
        // CHR-RAM carts attach a writable 8 KiB pattern window: reads use it (as
        // with attach_chr) and PPU $0000-$1FFF writes land in it. A CHR-ROM cart
        // leaves chr_ram_ empty, so those writes drop.
        void attach_chr_ram(std::span<std::uint8_t> ram) noexcept {
            chr_ = ram; // const view for the read path
            chr_ram_ = ram;
        }
        void attach_nametables(std::span<const std::uint8_t> ram) noexcept {
            external_nametables_ = ram;
        }
        // A separate CHR window for BACKGROUND fetches (MMC5's set-B banks). When
        // attached, background pattern fetches use it while 8x16 sprites are active
        // (the MMC5 sprite/background CHR split); sprites always use the main window.
        // Empty (the default) => background shares the main CHR window.
        void attach_chr_bg(std::span<const std::uint8_t> chr) noexcept { chr_bg_ = chr; }
        void set_mirroring(mirroring m) noexcept { mirroring_ = m; }
        // Select PAL (312-line) vs NTSC (262-line) frame geometry. The visible area
        // (0-239) and the vblank start (241) are identical; PAL only adds vblank
        // lines, lengthening the frame to 50 Hz. Config -- set once at assembly.
        void set_pal(bool pal) noexcept {
            total_lines_ = pal ? pal_lines_per_frame : lines_per_frame;
        }

        // Register access (CPU side). reg is the low 3 bits of the $2000..$3FFF
        // range (the caller masks the mirror).
        [[nodiscard]] std::uint8_t reg_read(std::uint8_t reg) noexcept;
        void reg_write(std::uint8_t reg, std::uint8_t value) noexcept;

        // Direct PPU-bus access -- used by the render pipeline and by tests to
        // poke VRAM without going through $2006/$2007.
        [[nodiscard]] std::uint8_t ppu_read(std::uint16_t addr) const noexcept;
        void ppu_write(std::uint16_t addr, std::uint8_t value) noexcept;

        // Direct OAM / palette seeding for tests (sprite-RAM and palette are
        // chip-internal; the CPU fills them through the ports on hardware).
        void poke_oam(std::size_t index, std::uint8_t value) noexcept {
            oam_[index % oam_size] = value;
        }
        [[nodiscard]] std::uint8_t peek_oam(std::size_t index) const noexcept {
            return oam_[index % oam_size];
        }

        // Is the NMI line asserted right now (vblank flag & CTRL NMI-enable)?
        [[nodiscard]] bool nmi_asserted() const noexcept { return nmi_line_; }

        // Fired at dot 0 of every line with its number.
        void set_scanline_callback(line_callback cb) noexcept { scanline_cb_ = std::move(cb); }
        // Fired when the beam enters vblank (after the frame renders).
        void set_vblank_callback(line_callback cb) noexcept { vblank_cb_ = std::move(cb); }

        // Beam position, for tests and debugging.
        [[nodiscard]] std::uint32_t beam_line() const noexcept { return scanline_; }
        [[nodiscard]] std::uint32_t beam_dot() const noexcept { return dot_; }

        // Whether background or sprite rendering is enabled (PPUMASK bits). The MMC3
        // scanline IRQ only clocks while rendering -- its counter is driven by A12
        // toggles that occur only during background/sprite fetches.
        [[nodiscard]] bool rendering_enabled() const noexcept {
            return (mask_ & (mask_bg_enable | mask_spr_enable)) != 0U;
        }

        // Force a full static render of the current state into the framebuffer
        // (a held-state per-line sweep from t_), without advancing the beam. For
        // tests / a host that wants the current frame without ticking; tick()
        // renders the live frame line-by-line.
        void render_frame() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(ppu2c02& owner) noexcept : tiles_(owner) {}
            [[nodiscard]] std::span<instrumentation::debug_layer* const> debug_layers() override {
                layer_ptr_[0] = &tiles_;
                return layer_ptr_;
            }

          private:
            class pattern_sheet_layer final : public instrumentation::debug_layer {
              public:
                explicit pattern_sheet_layer(ppu2c02& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::string_view name() const noexcept override {
                    return "pattern_table";
                }
                [[nodiscard]] frame_buffer_view view() const override;

              private:
                ppu2c02* owner_;
            };

            pattern_sheet_layer tiles_;
            std::array<instrumentation::debug_layer*, 1> layer_ptr_{};
        };

        // Fold a $2000..$2FFF nametable address onto the two physical 1 KB
        // banks per the active mirroring.
        [[nodiscard]] std::uint16_t nametable_index(std::uint16_t addr) const noexcept;
        // Palette RAM mirror: $10/$14/$18/$1C fold onto $00/$04/$08/$0C.
        [[nodiscard]] static std::uint16_t palette_index(std::uint16_t addr) noexcept;
        void refresh_nmi_line() noexcept;

        // Render one visible scanline `sy` from the loopy address `line_v` (+ fine
        // X): background then the sprites intersecting the line, into pixels_[sy].
        // CHR/nametable are read live, so a mid-frame bank switch takes effect from
        // the next line. Returns nothing; sprite-0 hit is scheduled via spr0_*.
        void render_bg_scanline(std::uint32_t sy, std::uint16_t line_v) noexcept;
        void render_sprites_scanline(std::uint32_t sy) noexcept;

        // Loopy scroll-address operators (the canonical 2C02 v/t mechanics).
        static void inc_coarse_x(std::uint16_t& v) noexcept;
        static void inc_y(std::uint16_t& v) noexcept;
        static void copy_horizontal(std::uint16_t& v, std::uint16_t t) noexcept;
        static void copy_vertical(std::uint16_t& v, std::uint16_t t) noexcept;

        // Decode one tile pixel (0..3) from CHR pattern memory.
        [[nodiscard]] std::uint32_t fetch_pattern_pixel(std::uint16_t pattern_base,
                                                        std::uint32_t tile, std::uint32_t fine_x,
                                                        std::uint32_t fine_y) const noexcept;
        // Read a background pattern byte ($0000-$1FFF), using the MMC5 set-B window
        // when it is attached and 8x16 sprites are active, else the main CHR window.
        [[nodiscard]] std::uint8_t bg_pattern_read(std::uint16_t addr) const noexcept;
        // 6-bit master-palette index -> 0x00RRGGBB.
        [[nodiscard]] static std::uint32_t master_rgb(std::uint8_t index) noexcept;
        // Final pixel colour from a palette entry, applying the PPUMASK greyscale
        // (index &= $30) and colour-emphasis (each set bit attenuates the other two
        // primaries) effects. All render paths funnel through this.
        [[nodiscard]] std::uint32_t shade(std::uint8_t master_index) const noexcept;

        std::vector<std::uint32_t> pixels_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(visible_width) * visible_height);
        // Background opacity of the scanline currently being rendered (pixel value
        // != 0), consumed by the sprite priority + sprite-0-hit logic for that line.
        std::array<std::uint8_t, visible_width> bg_opaque_{};
        // Sprite-0 hit is dot-scheduled: rendering a line finds the first sprite-0
        // / opaque-BG overlap dot; tick() sets the status flag when the beam reaches
        // it. -1 = no pending hit on the current line.
        int spr0_hit_dot_{-1};
        // The decoded pattern-table debug sheet, rebuilt on demand.
        mutable std::vector<std::uint32_t> pattern_sheet_;

        // Register state.
        std::uint8_t ctrl_{};
        std::uint8_t mask_{};
        std::uint8_t status_{};
        std::uint8_t oam_addr_{};

        // Internal loopy registers.
        std::uint16_t v_{}; // current VRAM address (15-bit; low 14 for access)
        std::uint16_t t_{}; // temp VRAM address
        std::uint8_t x_{};  // fine X scroll (3-bit)
        bool w_{};          // write latch: false = first write, true = second

        std::uint8_t read_buffer_{}; // delayed $2007 read buffer
        std::uint8_t open_bus_{};    // shared decay latch

        // Chip-internal memory.
        std::array<std::uint8_t, oam_size> oam_{};
        std::array<std::uint8_t, palette_size> palette_{};
        std::array<std::uint8_t, nametable_size> nametables_{};

        mirroring mirroring_{mirroring::horizontal};
        bool nmi_line_{};

        // Non-owning external memory.
        std::span<const std::uint8_t> chr_{};
        std::span<std::uint8_t> chr_ram_{};      // writable CHR (CHR-RAM carts); empty => CHR-ROM
        std::span<const std::uint8_t> chr_bg_{}; // MMC5 background CHR (set B); empty => share chr_
        std::span<const std::uint8_t> external_nametables_{};

        std::uint32_t dot_{};
        std::uint32_t scanline_{};
        std::uint32_t total_lines_{lines_per_frame}; // 262 (NTSC) or 312 (PAL)
        std::uint64_t frame_index_{};

        line_callback scanline_cb_{};
        line_callback vblank_cb_{};

        friend class introspection_surface;
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
