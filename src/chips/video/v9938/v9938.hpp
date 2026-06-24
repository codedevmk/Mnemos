#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Yamaha V9938 MSX-VIDEO (MSX2 VDP). This first Mnemos implementation covers
    // the CPU-visible contract needed by a real MSX2 machine shell: 128 KiB VRAM,
    // the standard four-port interface (#98-#9B), register and palette writes,
    // status/vblank IRQ, TMS-compatible text/graphics modes, and the MSX2 bitmap
    // modes most software reaches first (SCREEN 5 through SCREEN 8 style layouts).
    //
    // It includes a first-pass command engine, sprite renderer, interlace
    // presentation, and the hardware-visible lightpen/mouse register surface;
    // exact VDP command timing remains a follow-up hardware slice.
    class v9938 final : public ivideo, public immio {
      public:
        static constexpr int storage_width = 512;
        static constexpr int visible_height = 212;
        static constexpr int frame_height = visible_height * 2;
        static constexpr int vram_size = 0x20000; // 128 KiB, common MSX2 config
        static constexpr int register_count = 64;
        static constexpr int status_count = 10;
        static constexpr int palette_count = 16;
        static constexpr int cycles_per_line = 228;
        static constexpr int scanlines_ntsc = 262;
        static constexpr int scanlines_pal = 313;

        v9938() {
            introspection_.with_registers([this] { return register_snapshot(); });
            reset(reset_kind::power_on);
        }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frame_index_; }
        [[nodiscard]] frame_buffer_view framebuffer() const noexcept override;

        [[nodiscard]] std::uint8_t data_read() noexcept;
        void data_write(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t ctrl_read() noexcept;
        void ctrl_write(std::uint8_t value) noexcept;
        void palette_write(std::uint8_t value) noexcept;
        void register_indirect_write(std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override;
        void mmio_write(std::uint16_t offset, std::uint8_t value) override;

        void set_pal(bool pal) noexcept;
        [[nodiscard]] bool is_pal() const noexcept { return pal_mode_; }
        [[nodiscard]] int total_scanlines() const noexcept { return total_scanlines_; }

        void set_irq_callback(std::function<void(bool asserted)> cb) noexcept {
            irq_callback_ = std::move(cb);
        }
        [[nodiscard]] bool irq_asserted() const noexcept { return irq_asserted_; }
        void latch_lightpen(int x, int y, bool switch_pressed, bool second_field) noexcept;
        void latch_mouse_delta(std::int8_t delta_x, std::int8_t delta_y, bool switch_1,
                               bool switch_2) noexcept;

        [[nodiscard]] std::uint8_t reg(int index) const noexcept {
            return (index >= 0 && index < register_count) ? reg_[static_cast<std::size_t>(index)]
                                                          : 0U;
        }
        [[nodiscard]] std::uint8_t status() const noexcept { return status_[0]; }
        [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept { return vram_; }

        // Test/debug hook: render the current VRAM/register state without advancing
        // time. Runtime presentation normally happens at frame boundaries in tick().
        void render_frame() noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        enum class display_mode : std::uint8_t {
            graphic1,
            graphic2,
            graphic3,
            multicolor,
            text1,
            text2,
            bitmap4,
            bitmap2_512,
            bitmap4_512,
            bitmap7,
        };

        [[nodiscard]] display_mode active_mode() const noexcept;
        [[nodiscard]] int active_width() const noexcept;
        [[nodiscard]] int active_height() const noexcept;
        [[nodiscard]] int framebuffer_height() const noexcept;
        [[nodiscard]] bool interlace_enabled() const noexcept;
        [[nodiscard]] int framebuffer_y(int screen_y, int field) const noexcept;
        [[nodiscard]] int display_adjust_x() const noexcept;
        [[nodiscard]] int display_adjust_y() const noexcept;
        [[nodiscard]] int display_source_y(int screen_y) const noexcept;
        [[nodiscard]] std::uint32_t bitmap_display_base(display_mode mode) const noexcept;
        [[nodiscard]] std::uint32_t bitmap_page_stride(display_mode mode) const noexcept;
        [[nodiscard]] bool register_13_odd_phase_active() const noexcept;
        [[nodiscard]] std::uint32_t visible_bitmap_display_base(display_mode mode,
                                                                int field) const noexcept;
        [[nodiscard]] bool bw_output_enabled() const noexcept;
        [[nodiscard]] std::uint32_t display_rgb(std::uint32_t rgb) const noexcept;
        [[nodiscard]] std::uint32_t backdrop_rgb() const noexcept;
        [[nodiscard]] std::uint32_t palette_rgb(std::uint8_t index) const noexcept;
        [[nodiscard]] std::uint32_t palette_display_rgb(std::uint8_t index) const noexcept;
        [[nodiscard]] std::uint32_t sprite_rgb(std::uint8_t index) const noexcept;
        [[nodiscard]] bool color_zero_opaque() const noexcept;
        [[nodiscard]] std::uint32_t text2_blink_table_base() const noexcept;
        [[nodiscard]] bool text2_blink_phase_active() const noexcept;
        [[nodiscard]] std::uint32_t fixed_332_rgb(std::uint8_t value) const noexcept;
        [[nodiscard]] std::uint32_t vram_addr() const noexcept;
        [[nodiscard]] std::uint8_t status_register(std::uint8_t selected) const noexcept;
        [[nodiscard]] std::uint8_t status_register_2() const noexcept;
        [[nodiscard]] bool lightpen_enabled() const noexcept;
        [[nodiscard]] bool mouse_enabled() const noexcept;
        [[nodiscard]] bool pointing_device_active() const noexcept;
        [[nodiscard]] bool vram_address_auto_carries() const noexcept;
        void advance_vram_addr() noexcept;
        void write_register(std::uint8_t index, std::uint8_t value) noexcept;
        void update_irq() noexcept;
        void clear_pointing_device_status() noexcept;
        void write_frame_pixel(int x, int y, std::uint32_t rgb) noexcept;
        void write_field_pixel(int x, int y, int field, std::uint32_t rgb) noexcept;

        [[nodiscard]] int command_width() const noexcept;
        [[nodiscard]] int command_height() const noexcept;
        [[nodiscard]] std::uint16_t command_x(int low_reg, int high_reg) const noexcept;
        [[nodiscard]] std::uint16_t command_y(int low_reg, int high_reg) const noexcept;
        [[nodiscard]] std::uint16_t command_nx() const noexcept;
        [[nodiscard]] std::uint16_t command_ny() const noexcept;
        [[nodiscard]] int command_step_x() const noexcept;
        [[nodiscard]] int command_step_y() const noexcept;
        [[nodiscard]] std::uint8_t command_color_mask() const noexcept;
        [[nodiscard]] int high_speed_pixel_group_size() const noexcept;
        [[nodiscard]] int high_speed_aligned_x(int x) const noexcept;
        [[nodiscard]] std::uint16_t high_speed_aligned_nx() const noexcept;
        [[nodiscard]] std::uint8_t command_pixel(int x, int y) const noexcept;
        [[nodiscard]] std::uint8_t command_logic(std::uint8_t source,
                                                 std::uint8_t destination) const noexcept;
        void set_command_pixel(int x, int y, std::uint8_t value, bool logical) noexcept;
        void set_high_speed_byte(int x, int y, int step_x, std::uint8_t value) noexcept;
        void finish_command() noexcept;
        void start_command(std::uint8_t value) noexcept;
        void begin_cpu_transfer(bool high_speed) noexcept;
        void command_stream_write(std::uint8_t value) noexcept;
        void begin_cpu_read_transfer() noexcept;
        void prepare_cpu_read_transfer() noexcept;
        void command_stream_read() noexcept;
        void command_fill(bool high_speed, bool logical) noexcept;
        void command_y_copy() noexcept;
        void command_copy(bool logical, bool high_speed) noexcept;
        void command_line() noexcept;
        void command_search() noexcept;
        void command_pset() noexcept;
        void command_point() noexcept;

        void render_text(int columns, int cell_width) noexcept;
        void render_graphic1() noexcept;
        void render_graphic2() noexcept;
        void render_multicolor() noexcept;
        void render_bitmap4() noexcept;
        void render_bitmap2_512() noexcept;
        void render_bitmap4_512() noexcept;
        void render_bitmap7() noexcept;
        void render_sprites() noexcept;
        void apply_display_adjust() noexcept;

        std::array<std::uint8_t, vram_size> vram_{};
        std::array<std::uint8_t, register_count> reg_{};
        std::array<std::uint8_t, status_count> status_{};
        std::array<std::uint32_t, palette_count> palette_{};

        std::uint16_t addr_{};
        std::uint8_t code_{};
        std::uint8_t read_buffer_{};
        bool ctrl_pending_{};
        std::uint8_t ctrl_first_{};
        bool palette_pending_{};
        std::uint8_t palette_first_{};
        std::uint8_t palette_index_{};

        int scanline_cycle_{};
        int scanline_{};
        int total_scanlines_{scanlines_ntsc};
        bool pal_mode_{};
        bool irq_asserted_{};
        std::function<void(bool)> irq_callback_{};

        std::uint8_t command_code_{};
        bool command_cpu_transfer_{};
        bool command_cpu_read_transfer_{};
        bool command_high_speed_transfer_{};
        std::uint16_t command_stream_x_{};
        std::uint16_t command_stream_y_{};

        std::uint64_t frame_index_{};
        std::uint64_t blink_start_frame_{};
        std::vector<std::uint32_t> framebuffer_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(storage_width) * frame_height);
        std::vector<std::uint32_t> display_adjust_scratch_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(storage_width) * frame_height);
        std::vector<std::uint8_t> sprite_occupancy_ =
            std::vector<std::uint8_t>(static_cast<std::size_t>(storage_width) * visible_height);

        std::array<register_descriptor, 12> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::video
