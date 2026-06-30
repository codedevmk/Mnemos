#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    class v9938 final : public ivideo, public immio {
      public:
        static constexpr int max_width = 512;
        static constexpr int max_height = 424;
        static constexpr int display_width_256 = 256;
        static constexpr int display_height_192 = 192;
        static constexpr int display_height_212 = 212;
        static constexpr int display_height_384 = 384;
        static constexpr int display_height_424 = 424;
        static constexpr int vram_size = 0x20000;
        static constexpr int expanded_vram_size = 0x10000;
        static constexpr int register_count = 64;
        static constexpr int status_register_count = 10;
        static constexpr int palette_count = 16;
        static constexpr int cycles_per_line = 228;
        static constexpr int scanlines_ntsc = 262;
        static constexpr int scanlines_pal = 313;

        enum class display_mode : std::uint8_t {
            graphics_i,
            text_i,
            text_ii,
            multicolor,
            graphics_ii,
            graphics_iii,
            graphics_iv,
            graphics_v,
            graphics_vi,
            graphics_vii,
        };

        v9938() { reset(reset_kind::power_on); }

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
        [[nodiscard]] std::uint8_t status_read() noexcept;
        void ctrl_write(std::uint8_t value) noexcept;
        void palette_write(std::uint8_t value) noexcept;
        void indirect_reg_write(std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override;
        void mmio_write(std::uint16_t offset, std::uint8_t value) override;

        void set_pal(bool pal) noexcept;
        [[nodiscard]] bool is_pal() const noexcept { return pal_mode_; }

        void set_irq_callback(std::function<void(bool asserted)> cb) noexcept {
            irq_callback_ = std::move(cb);
        }
        [[nodiscard]] bool irq_asserted() const noexcept { return irq_asserted_; }

        [[nodiscard]] display_mode mode() const noexcept;
        [[nodiscard]] int visible_width() const noexcept;
        [[nodiscard]] int visible_height() const noexcept;
        [[nodiscard]] int total_scanlines() const noexcept { return total_scanlines_; }
        [[nodiscard]] std::uint8_t reg(int index) const noexcept {
            return (index >= 0 && index < register_count) ? reg_[static_cast<std::size_t>(index)]
                                                          : 0U;
        }
        [[nodiscard]] std::uint8_t status(int index) const noexcept {
            if (index < 0 || index >= status_register_count) {
                return 0xFFU;
            }
            return compose_status_register(static_cast<std::uint8_t>(index));
        }
        [[nodiscard]] std::uint16_t palette(int index) const noexcept {
            return (index >= 0 && index < palette_count) ? palette_[static_cast<std::size_t>(index)]
                                                         : 0U;
        }
        [[nodiscard]] std::uint32_t cpu_vram_address() const noexcept { return vram_address(); }
        [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept { return vram_; }

        void render_frame() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(v9938& owner) noexcept
                : vram_view_("vram", owner.vram_),
                  expanded_vram_view_("expanded_vram", owner.expanded_vram_),
                  regs_view_("registers", owner.reg_),
                  status_view_("status", owner.status_),
                  palette_view_("palette", owner.palette_bytes_) {
                memory_views_[0] = &vram_view_;
                memory_views_[1] = &expanded_vram_view_;
                memory_views_[2] = &regs_view_;
                memory_views_[3] = &status_view_;
                memory_views_[4] = &palette_view_;
            }

            [[nodiscard]] std::span<instrumentation::memory_view* const> memory_views() override {
                return memory_views_;
            }

          private:
            instrumentation::span_memory_view vram_view_;
            instrumentation::span_memory_view expanded_vram_view_;
            instrumentation::span_memory_view regs_view_;
            instrumentation::span_memory_view status_view_;
            instrumentation::span_memory_view palette_view_;
            std::array<instrumentation::memory_view*, 5> memory_views_{};
        };

        enum class command_stream_kind : std::uint8_t {
            none,
            hmmc,
            lmmc,
            lmcm,
        };

        void sync_palette_entry(std::size_t index) noexcept;
        void sync_palette_bytes() noexcept;
        [[nodiscard]] std::uint32_t palette_rgb(std::uint8_t colour) const noexcept;
        [[nodiscard]] std::uint32_t paletted_display_rgb(std::uint8_t colour) const noexcept;
        [[nodiscard]] std::uint32_t backdrop_rgb() const noexcept;
        [[nodiscard]] std::uint32_t fixed_rgb(std::uint8_t colour) const noexcept;
        [[nodiscard]] std::uint32_t display_rgb(std::uint32_t rgb) const noexcept;
        [[nodiscard]] std::uint8_t vram_at(std::uint32_t address) const noexcept {
            return vram_[address & (vram_size - 1U)];
        }
        [[nodiscard]] std::uint8_t memory_at(std::uint32_t address,
                                             bool expansion) const noexcept;
        void write_memory(std::uint32_t address, bool expansion, std::uint8_t value) noexcept;
        [[nodiscard]] bool cpu_access_uses_expansion() const noexcept;
        [[nodiscard]] bool command_source_uses_expansion() const noexcept;
        [[nodiscard]] bool command_destination_uses_expansion() const noexcept;
        [[nodiscard]] std::uint32_t vram_address() const noexcept;
        [[nodiscard]] bool interlace_enabled() const noexcept;
        [[nodiscard]] int field_visible_height() const noexcept;
        [[nodiscard]] int display_line_to_field_line(int line) const noexcept;
        [[nodiscard]] bool display_line_uses_second_field(int line) const noexcept;
        [[nodiscard]] bool vram_access_auto_increments_base() const noexcept;
        void set_vram_address(std::uint32_t address) noexcept;
        void increment_vram_address() noexcept;
        void write_register(std::uint8_t reg, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t compose_status_register(std::uint8_t index) const noexcept;
        [[nodiscard]] std::uint8_t compose_status2() const noexcept;
        [[nodiscard]] bool vertical_retrace_active() const noexcept;
        [[nodiscard]] bool horizontal_retrace_active() const noexcept;
        void update_irq() noexcept;
        void finish_scanline() noexcept;
        void render_scanline(int line) noexcept;
        void render_unadjusted_scanline(int line, bool second_field, std::uint32_t* out) noexcept;
        void render_graphics_i_scanline(int line, std::uint32_t* out) noexcept;
        void render_graphics_ii_scanline(int line, std::uint32_t* out) noexcept;
        void render_text_i_scanline(int line, std::uint32_t* out) noexcept;
        void render_text_ii_scanline(int line, std::uint32_t* out) noexcept;
        void render_multicolor_scanline(int line, std::uint32_t* out) noexcept;
        void render_graphics_iv_scanline(int line, bool second_field, std::uint32_t* out) noexcept;
        void render_graphics_v_scanline(int line, bool second_field, std::uint32_t* out) noexcept;
        void render_graphics_vi_scanline(int line, bool second_field, std::uint32_t* out) noexcept;
        void render_graphics_vii_scanline(int line, bool second_field, std::uint32_t* out) noexcept;
        void render_sprites_mode1(int line, std::uint32_t* out) noexcept;
        void render_sprites_mode2(int line, std::uint32_t* out) noexcept;
        [[nodiscard]] int vertical_scrolled_line(int line) const noexcept;
        [[nodiscard]] bool second_display_field() const noexcept;
        [[nodiscard]] bool alternate_display_uses_odd_page(std::uint8_t selected_page,
                                                           bool second_field) const noexcept;
        [[nodiscard]] std::uint32_t
        active_bitmap_base_graphics_iv(bool second_field) const noexcept;
        [[nodiscard]] std::uint32_t
        active_bitmap_base_graphics_vi(bool second_field) const noexcept;
        [[nodiscard]] bool text_ii_cell_blink_enabled(int row, int col) const noexcept;
        [[nodiscard]] bool text_ii_blink_phase_active() const noexcept;
        void execute_command(std::uint8_t command) noexcept;
        void execute_hmmv() noexcept;
        void execute_hmmc() noexcept;
        void execute_ymmm() noexcept;
        void execute_hmmm() noexcept;
        void execute_lmmc(std::uint8_t op) noexcept;
        void execute_lmcm() noexcept;
        void execute_lmmv(std::uint8_t op) noexcept;
        void execute_lmmm(std::uint8_t op) noexcept;
        void execute_line(std::uint8_t op) noexcept;
        void execute_srch() noexcept;
        void execute_pset(std::uint8_t op) noexcept;
        void execute_point() noexcept;
        void stop_command() noexcept;
        void arm_command_busy(std::uint64_t cycles) noexcept;
        void arm_transfer_delay(std::uint64_t cycles) noexcept;
        void advance_command_timers(std::uint64_t cycles) noexcept;
        [[nodiscard]] std::uint64_t
        apply_command_access_pressure(std::uint64_t cycles) const noexcept;
        void start_cpu_to_vram_command(command_stream_kind kind, std::uint8_t op) noexcept;
        void start_vram_to_cpu_command() noexcept;
        void consume_cpu_command_data(std::uint8_t value) noexcept;
        void prepare_vram_to_cpu_data() noexcept;
        void advance_command_stream(std::uint16_t pixels) noexcept;
        void update_command_stream_status() noexcept;
        [[nodiscard]] std::uint64_t
        estimate_command_busy_cycles(std::uint8_t command) const noexcept;
        [[nodiscard]] std::uint64_t
        estimate_cpu_transfer_delay_cycles(command_stream_kind kind) const noexcept;
        [[nodiscard]] std::uint16_t command_x(int low_reg, int high_reg) const noexcept;
        [[nodiscard]] std::uint16_t command_y(int low_reg, int high_reg) const noexcept;
        void set_command_x(int low_reg, int high_reg, std::uint16_t value) noexcept;
        void set_command_y(int low_reg, int high_reg, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t command_screen_width() const noexcept;
        [[nodiscard]] std::uint16_t command_screen_height() const noexcept;
        [[nodiscard]] std::uint16_t executed_vertical_rows(std::uint16_t y, std::uint16_t rows,
                                                           int y_step) const noexcept;
        [[nodiscard]] std::uint16_t executed_vertical_rows(std::uint16_t y0, std::uint16_t y1,
                                                           std::uint16_t rows,
                                                           int y_step) const noexcept;
        void apply_command_register_postconditions(std::uint8_t command) noexcept;
        [[nodiscard]] std::uint8_t high_speed_pixels_per_byte(display_mode mode) const noexcept;
        [[nodiscard]] std::uint8_t high_speed_colour_from_byte(std::uint8_t value, int x,
                                                               display_mode mode) const noexcept;
        [[nodiscard]] bool command_pixel_address(std::uint16_t x, std::uint16_t y,
                                                 std::uint32_t& address, std::uint8_t& shift,
                                                 std::uint8_t& mask) const noexcept;
        [[nodiscard]] std::uint8_t read_command_pixel(std::uint16_t x, std::uint16_t y,
                                                      bool expansion) const noexcept;
        [[nodiscard]] std::uint8_t read_high_speed_byte(std::uint16_t x, std::uint16_t y,
                                                        bool expansion) const noexcept;
        void write_high_speed_byte(std::uint16_t x, std::uint16_t y, std::uint8_t value,
                                   display_mode mode, bool expansion) noexcept;
        void write_high_speed_byte(std::uint16_t x, std::uint16_t y, std::uint8_t value) noexcept;
        void write_command_pixel(std::uint16_t x, std::uint16_t y, std::uint8_t colour,
                                 bool expansion, std::uint8_t logical_op = 0U) noexcept;
        [[nodiscard]] std::uint8_t apply_logical(std::uint8_t source, std::uint8_t dest,
                                                 std::uint8_t op, std::uint8_t mask) const noexcept;

        std::array<std::uint8_t, vram_size> vram_{};
        std::array<std::uint8_t, expanded_vram_size> expanded_vram_{};
        std::array<std::uint8_t, register_count> reg_{};
        std::array<std::uint8_t, status_register_count> status_{};
        std::array<std::uint16_t, palette_count> palette_{};
        std::array<std::uint8_t, palette_count * 2> palette_bytes_{};

        std::uint16_t addr_low_{};
        std::uint8_t code_{};
        bool cmd_pending_{};
        std::uint8_t cmd_first_{};
        std::uint8_t read_buffer_{};
        bool palette_second_{};
        std::uint8_t palette_first_{};

        command_stream_kind command_stream_{command_stream_kind::none};
        std::uint16_t stream_x_{};
        std::uint16_t stream_y_{};
        std::uint16_t stream_nx_{};
        std::uint16_t stream_ny_{};
        std::uint16_t stream_col_{};
        std::uint16_t stream_row_{};
        int stream_x_step_{1};
        int stream_y_step_{1};
        std::uint8_t stream_op_{};
        bool stream_high_speed_{};
        bool stream_source_expansion_{};
        bool stream_dest_expansion_{};
        display_mode stream_mode_{display_mode::graphics_i};
        std::uint64_t command_busy_cycles_{};
        std::uint64_t stream_ready_delay_cycles_{};

        bool irq_asserted_{};
        std::function<void(bool)> irq_callback_{};

        int scanline_{};
        int scanline_cycle_{};
        int total_scanlines_{scanlines_ntsc};
        bool pal_mode_{};
        std::uint64_t frame_index_{};

        std::vector<std::uint32_t> framebuffer_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(max_width) * max_height);

        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
