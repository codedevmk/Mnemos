#pragma once

#include <mnemos/chips/common/chip.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // MOS 6569 / 6567 (VIC-II) — the Commodore 64 video interface controller.
    //
    // Ported from the Emu reference core (see ADR 0006 and NOTES.md). This is the
    // register + raster-timing surface every C64 program writes against: the full
    // $D000-$D02E register path ($D02F-$D03F read $FF), the Pepto 16-colour
    // palette, the raster counter + 9-bit raster compare, the beam-position
    // tracker (PAL 6569 / NTSC 6567), the raster-IRQ source/mask/master with
    // write-1 acknowledge, bad-line detection with BA / CPU-stall windows, the
    // SCROLY/SCROLX mode decoders, sprite X/Y latches, and the internal
    // video-matrix address generator (VC/VCBASE/RC/VMLI).
    //
    // The video memory the VIC fetches from: 64K main RAM, the 4K character ROM,
    // and the 1K colour RAM (low nybble used). The VIC sees a 16K bank of `ram`
    // selected by set_bank, with the character ROM shadowed at VIC $1000-$1FFF in
    // banks 0 and 2. Spans are borrowed; the owner outlives the VIC.
    struct vic_memory final {
        std::span<const std::uint8_t> ram;       // 64 KiB main memory
        std::span<const std::uint8_t> char_rom;  // 4 KiB character generator
        std::span<const std::uint8_t> color_ram; // 1 KiB colour RAM (4-bit)
    };

    // Sprite compositing, multicolour bitmap, and extended-colour text are
    // deferred follow-up work; the scanline renderer covers border, hi-res and
    // multicolour text, and standard bitmap.
    class vic_ii_6569 final : public i_video, public i_mmio {
      public:
        // Silicon revision. Within a video standard only the early NTSC 6567R56A
        // changes CPU-visible geometry (64 cycles x 262 lines vs the 6567R8's
        // 65 x 263); all PAL parts are 63 x 312.
        enum class revision : std::uint8_t {
            pal_6569,      // PAL, 63 cyc x 312 lines (C64 default)
            pal_8565,      // PAL HMOS (C64C); same timing as 6569
            ntsc_6567r8,   // NTSC production, 65 cyc x 263 lines
            ntsc_6567r56a, // early NTSC, 64 cyc x 262 lines
        };

        // Decoded view of SCROLY ($D011) + SCROLX ($D016), refreshed on write.
        struct mode_flags final {
            bool ecm{};
            bool bmm{};
            bool den{};
            bool rsel{};
            std::uint8_t yscroll{};
            bool res{};
            bool mcm{};
            bool csel{};
            std::uint8_t xscroll{};
        };

        static constexpr std::uint8_t register_count = 0x40U;
        static constexpr std::uint8_t sprite_count = 8U;

        vic_ii_6569() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        // i_video: frame counter + framebuffer view (see base class).
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // Attach the memory the VIC fetches graphics from, and select the 16K bank
        // (0..3, as driven by the inverted CIA2 port-A low bits). Without attached
        // memory the renderer fills the whole raster with the border colour.
        void attach_memory(const vic_memory& memory) noexcept;
        void set_bank(std::uint8_t bank) noexcept;
        [[nodiscard]] std::uint8_t bank() const noexcept { return bank_; }

        // Machine configuration: selects the line/cycle geometry. Survives reset.
        void set_revision(revision rev) noexcept;
        [[nodiscard]] revision rev() const noexcept { return rev_; }
        [[nodiscard]] bool is_pal() const noexcept { return is_pal_; }

        // MMIO register access over the low 6 bits of the $D000 window. read is
        // non-const: reading the collision latches ($D01E/$D01F) clears them.
        [[nodiscard]] std::uint8_t read(std::uint8_t address) noexcept;
        void write(std::uint8_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return read(static_cast<std::uint8_t>(offset));
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            write(static_cast<std::uint8_t>(offset), value);
        }

        // Beam scheduler: advance by `cycles` VIC cycles (63/line PAL, 65 NTSC).
        // (tick is the i_chip entry point and forwards here.)

        void trigger_light_pen(std::uint16_t x, std::uint16_t y) noexcept;
        void set_raster(std::uint16_t line) noexcept; // test/debug: jump the beam

        [[nodiscard]] std::uint16_t raster_y() const noexcept { return raster_y_; }
        [[nodiscard]] std::uint16_t raster_x() const noexcept { return raster_x_; }
        [[nodiscard]] std::uint16_t raster_compare() const noexcept { return raster_compare_; }
        [[nodiscard]] const mode_flags& modes() const noexcept { return modes_; }
        [[nodiscard]] std::uint16_t sprite_x(std::uint8_t index) const noexcept {
            return sprite_x_[index & 0x07U];
        }
        [[nodiscard]] std::uint8_t sprite_y(std::uint8_t index) const noexcept {
            return sprite_y_[index & 0x07U];
        }

        [[nodiscard]] std::uint16_t total_lines() const noexcept;
        [[nodiscard]] std::uint16_t cycles_per_line() const noexcept;

        // Framebuffer geometry: the full raster (cycles_per_line * 8 by total_lines).
        [[nodiscard]] std::uint32_t frame_width() const noexcept;
        [[nodiscard]] std::uint32_t frame_height() const noexcept;
        [[nodiscard]] bool irq_asserted() const noexcept;
        [[nodiscard]] bool bad_line_condition() const noexcept;
        [[nodiscard]] bool ba_low() const noexcept;
        [[nodiscard]] bool cpu_read_stalled() const noexcept;

        [[nodiscard]] static std::uint32_t color_rgb888(std::uint8_t color_index) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        [[nodiscard]] std::uint8_t current_irq_sources() const noexcept;
        void decode_modes() noexcept;
        void refresh_sprite_x() noexcept;
        void refresh_sprite_y() noexcept;
        void refresh_raster_compare() noexcept;
        void update_irq_registers() noexcept;
        void latch_irq_source(std::uint8_t source) noexcept;
        void refresh_raster_irq_edge() noexcept;
        void update_den_latch() noexcept;
        void advance_video_counters(std::uint16_t cycle) noexcept;

        // Scanline renderer. render_line fills framebuffer row `y` from the current
        // register + memory state; ensure_framebuffer (re)sizes it to the geometry.
        void ensure_framebuffer();
        void render_line(std::uint16_t y) noexcept;
        [[nodiscard]] std::uint8_t fetch(std::uint16_t vic_address) const noexcept;

        std::array<std::uint8_t, register_count> regs_{};
        std::array<std::uint16_t, sprite_count> sprite_x_{};
        std::array<std::uint8_t, sprite_count> sprite_y_{};
        std::uint16_t raster_compare_{};
        std::uint16_t raster_y_{};
        std::uint16_t raster_x_{};
        mode_flags modes_{};
        bool is_pal_{true};
        revision rev_{revision::pal_6569};
        bool den_latched_line_30_{};

        // Internal video-matrix address generator (Bauer §3.7.2).
        std::uint16_t vc_{};
        std::uint16_t vcbase_{};
        std::uint8_t rc_{};
        std::uint8_t vmli_{};
        bool display_state_{};

        bool raster_match_active_{};

        // Frame output.
        std::vector<std::uint32_t> framebuffer_; // frame_width * frame_height, ARGB-ignored
        std::uint32_t fb_width_{};
        std::uint32_t fb_height_{};
        std::uint64_t frame_index_{};

        // VIC fetch memory (borrowed) and the selected 16K bank.
        vic_memory memory_{};
        std::uint8_t bank_{};

        std::array<register_descriptor, 5> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::video
