#pragma once

#include "chip.hpp"
#include "introspection_views.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Sinclair ZX Spectrum ULA (48K) -- the display + interrupt half of the
    // machine. As an ivideo frame source it is ticked once per Z80 T-state; it
    // counts a 69888-T-state frame (224 x 312), renders the 256x192 bitmap +
    // attribute display (with the FLASH attribute swap and a flat border) into a
    // 0x00RRGGBB framebuffer at each frame boundary, and pulses the Z80 /INT line
    // for 32 T-states at the start of every frame (50 Hz).
    //
    // The ULA borrows the machine's RAM (the screen lives at $4000-$5AFF) through
    // set_screen_ram, drives the CPU /INT through an injected callback (so it does
    // not depend on the Z80 type), and takes its border colour from the port-$FE
    // write the host routes in via set_border. Contention, the beeper, and the
    // 128K/Timex paths are out of scope here.
    //
    // Ported from the Emu reference (systems/sinclair/spectrum); clean-room per
    // the public ZX Spectrum hardware description, no third-party emulator source.
    class ula final : public ivideo {
      public:
        static constexpr int display_width = 256;
        static constexpr int display_height = 192;
        static constexpr int tstates_per_line = 224;
        static constexpr int scanlines_per_frame = 312;
        static constexpr int tstates_per_frame = tstates_per_line * scanlines_per_frame; // 69888
        static constexpr int irq_pulse_tstates = 32;
        // Visible raster (border + screen), matching the Emu 48K frame geometry.
        static constexpr int frame_width = tstates_per_line * 2;                    // 448
        static constexpr int frame_height = scanlines_per_frame;                    // 312
        static constexpr int screen_x_offset = (frame_width - display_width) / 2;   // 96
        static constexpr int screen_y_offset = (frame_height - display_height) / 2; // 60
        static constexpr std::size_t screen_ram_bytes = 0x1B00;                     // $4000-$5AFF

        using irq_line_fn = std::function<void(bool asserted)>;

        ula() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        // ivideo.
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        // Manifest wiring (called once after construction).
        void set_screen_ram(std::span<const std::uint8_t> ram) noexcept { screen_ram_ = ram; }
        void set_irq_callback(irq_line_fn fn) noexcept { irq_callback_ = std::move(fn); }

        // Port $FE write: border colour (bits 0-2). MIC/EAR (bits 3-4) are the
        // beeper, not modelled here.
        void set_border(std::uint8_t value) noexcept {
            border_ = static_cast<std::uint8_t>(value & 0x07U);
        }
        [[nodiscard]] std::uint8_t border() const noexcept { return border_; }

        // Force a render of the current screen RAM into the framebuffer (used by
        // tests / a host that wants the frame without advancing time).
        void render_frame() noexcept;

      private:
        void drive_irq(bool asserted) noexcept;

        struct introspection_surface final : public instrumentation::ichip_introspection {};

        std::span<const std::uint8_t> screen_ram_{};
        irq_line_fn irq_callback_{};
        std::vector<std::uint32_t> framebuffer_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(frame_width) * frame_height);

        std::uint8_t border_{};
        int frame_tstates_{};
        int irq_pulse_{};
        std::uint32_t frame_count_{}; // FLASH phase: bit 4 toggles every 16 frames
        std::uint64_t frame_index_{};
        bool irq_asserted_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::video
