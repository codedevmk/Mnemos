#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::chips::video {

    // Texas Instruments TMS9918A/TMS9928A/TMS9929A VDP, the MSX1 video contract:
    // 16 KiB VRAM, eight write-only registers, a buffered VRAM data port, a status
    // register, Graphics I/II, Text, Multicolor, and 32 single-colour sprites with
    // the 4-sprites-per-line limit. The chip is ticked at the Z80 clock and acts as
    // the frame source for the MSX scheduler.
    class tms9918a final : public ivideo, public immio {
      public:
        static constexpr int display_width = 256;
        static constexpr int display_height = 192;
        static constexpr int vram_size = 0x4000;
        static constexpr int register_count = 8;
        static constexpr int sprite_count = 32;
        static constexpr int cycles_per_line = 228;
        static constexpr int scanlines_ntsc = 262;
        static constexpr int scanlines_pal = 313;

        enum class display_mode : std::uint8_t {
            graphics_i,
            text,
            multicolor,
            graphics_ii,
        };

        tms9918a() { reset(reset_kind::power_on); }

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

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return (offset & 1U) != 0U ? status_read() : data_read();
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

        void set_irq_callback(std::function<void(bool asserted)> cb) noexcept {
            irq_callback_ = std::move(cb);
        }
        [[nodiscard]] bool irq_asserted() const noexcept { return irq_asserted_; }

        [[nodiscard]] display_mode mode() const noexcept;
        [[nodiscard]] std::uint8_t reg(int index) const noexcept {
            return (index >= 0 && index < register_count) ? reg_[static_cast<std::size_t>(index)]
                                                          : 0U;
        }
        [[nodiscard]] std::uint8_t status() const noexcept { return status_; }
        [[nodiscard]] std::uint16_t cpu_vram_address() const noexcept { return addr_; }
        [[nodiscard]] std::span<const std::uint8_t> vram() const noexcept { return vram_; }

        // Test/debug helper: render the current VRAM/register state without
        // advancing time.
        void render_frame() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(tms9918a& owner) noexcept
                : vram_view_("vram", owner.vram_),
                  regs_view_("registers", owner.reg_),
                  registers_(owner) {
                memory_views_[0] = &vram_view_;
                memory_views_[1] = &regs_view_;
            }

            [[nodiscard]] std::span<instrumentation::memory_view* const> memory_views() override {
                return memory_views_;
            }

            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_;
            }

          private:
            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(tms9918a& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override {
                    for (std::size_t i = 0; i < owner_->reg_.size(); ++i) {
                        snapshot_[i] = {k_names[i], owner_->reg_[i], 8U};
                    }
                    snapshot_[8] = {"STATUS", owner_->status_, 8U, register_value_format::flags};
                    snapshot_[9] = {"ADDR", owner_->addr_, 14U};
                    snapshot_[10] = {"CODE", owner_->code_, 2U};
                    snapshot_[11] = {"FRAME", owner_->frame_index_, 32U};
                    snapshot_[12] = {"SCANLINE", static_cast<std::uint64_t>(owner_->scanline_),
                                     16U};
                    return snapshot_;
                }

              private:
                static constexpr std::array<std::string_view, register_count> k_names{
                    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7"};
                tms9918a* owner_;
                std::array<register_descriptor, register_count + 5U> snapshot_{};
            };

            instrumentation::span_memory_view vram_view_;
            instrumentation::span_memory_view regs_view_;
            registers_impl registers_;
            std::array<instrumentation::memory_view*, 2> memory_views_{};
        };

        [[nodiscard]] std::uint32_t palette_rgb(std::uint8_t colour) const noexcept;
        [[nodiscard]] std::uint8_t vram_at(std::uint16_t address) const noexcept {
            return vram_[address & 0x3FFFU];
        }
        void render_scanline(int line) noexcept;
        void render_graphics_i_scanline(int line, std::uint32_t* out) noexcept;
        void render_graphics_ii_scanline(int line, std::uint32_t* out) noexcept;
        void render_text_scanline(int line, std::uint32_t* out) noexcept;
        void render_multicolor_scanline(int line, std::uint32_t* out) noexcept;
        void render_sprites(int line, std::uint32_t* out) noexcept;
        void update_irq() noexcept;
        void finish_scanline() noexcept;

        std::array<std::uint8_t, vram_size> vram_{};
        std::array<std::uint8_t, register_count> reg_{};

        std::uint16_t addr_{};
        std::uint8_t code_{};
        bool cmd_pending_{};
        std::uint8_t cmd_first_{};
        std::uint8_t read_buffer_{};

        std::uint8_t status_{};
        bool irq_asserted_{};
        std::function<void(bool)> irq_callback_{};

        int scanline_{};
        int scanline_cycle_{};
        int total_scanlines_{scanlines_ntsc};
        bool pal_mode_{};
        std::uint64_t frame_index_{};

        std::vector<std::uint32_t> framebuffer_ =
            std::vector<std::uint32_t>(static_cast<std::size_t>(display_width) * display_height);

        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::video
