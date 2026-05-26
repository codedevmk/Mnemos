#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Sega Master System VDP (315-5124, a TMS9918A derivative). Implements Mode 4:
    // a 32x28 name table with
    // per-tile priority / palette / flip, 4bpp planar tiles, full-screen H/V
    // scrolling with the row/column locks, 64 sprites (8x8 or 8x16, optional zoom,
    // 8-per-line limit with overflow + collision flags), the two-byte control-port
    // command protocol, the V/H counters, and the line + frame interrupts.
    //
    // 16 KiB VRAM + 32-entry CRAM (--BBGGRR). The scanline renderer fills an
    // 0x00RRGGBB framebuffer. As an ivideo frame source the VDP is ticked per Z80
    // cycle (228 per scanline); frame_index increments once per rendered frame.
    class sms_vdp final : public ivideo, public immio {
      public:
        static constexpr int fb_width = 256;
        static constexpr int fb_height = 240;
        static constexpr int vram_size = 0x4000;
        static constexpr int cram_size = 32;
        static constexpr int register_count = 16;
        static constexpr int cycles_per_line = 228; // Z80 cycles
        static constexpr int scanlines_ntsc = 262;
        static constexpr int scanlines_pal = 313;

        sms_vdp() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // ivideo.
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // VDP ports. The SMS wires Z80 $BE -> data, $BF -> control, $7E -> V, $7F -> H.
        [[nodiscard]] std::uint8_t data_read() noexcept;
        void data_write(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t ctrl_read() noexcept;
        void ctrl_write(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t vcounter() const noexcept;
        [[nodiscard]] std::uint8_t hcounter() const noexcept;

        // immio convenience: even offset = data ($BE), odd = control ($BF).
        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return (offset & 1U) != 0U ? ctrl_read() : data_read();
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            if ((offset & 1U) != 0U) {
                ctrl_write(value);
            } else {
                data_write(value);
            }
        }

        void set_pal(bool pal) noexcept;
        [[nodiscard]] bool is_pal() const noexcept { return pal_mode_; }

        // /INT line: fired on every transition; the SMS ORs it into the Z80 IRQ.
        void set_irq_callback(std::function<void(bool asserted)> cb) noexcept {
            irq_callback_ = std::move(cb);
        }
        [[nodiscard]] bool irq_asserted() const noexcept {
            return frame_irq_pending_ || line_irq_pending_;
        }

        [[nodiscard]] int visible_height() const noexcept;
        [[nodiscard]] std::uint8_t reg(int index) const noexcept {
            return (index >= 0 && index < register_count) ? reg_[static_cast<std::size_t>(index)]
                                                          : 0U;
        }
        [[nodiscard]] std::uint8_t status() const noexcept { return status_; }
        [[nodiscard]] int scanline() const noexcept { return scanline_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        void begin_scanline() noexcept;             // render the current scanline if visible
        [[nodiscard]] bool run_scanline() noexcept; // advance + interrupts; returns IRQ edge
        void render_scanline(int line) noexcept;
        void fill_scanline_bg(int line) noexcept;
        void update_irq() noexcept;

        std::array<std::uint8_t, vram_size> vram_{};
        std::array<std::uint8_t, cram_size> cram_{};
        std::array<std::uint8_t, register_count> reg_{};

        std::uint16_t addr_{};
        std::uint8_t code_{};
        bool cmd_pending_{};
        std::uint8_t cmd_first_{};
        std::uint8_t read_buffer_{};

        int scanline_{};
        int scanline_cycle_{};
        int total_scanlines_{scanlines_ntsc};
        bool pal_mode_{};

        bool frame_irq_pending_{};
        bool line_irq_pending_{};
        int line_counter_{};
        std::uint8_t status_{};

        bool irq_last_{};
        std::function<void(bool)> irq_callback_{};

        std::vector<std::uint32_t> framebuffer_; // fb_width * fb_height, 0x00RRGGBB
        std::uint64_t frame_index_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::video
