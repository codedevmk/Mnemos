#include "upd94244.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        [[nodiscard]] std::uint8_t sample_byte(std::span<const std::uint8_t> data,
                                               std::uint64_t index,
                                               std::uint8_t fallback) noexcept {
            if (data.empty()) {
                return fallback;
            }
            return data[static_cast<std::size_t>(index % data.size())];
        }

        [[nodiscard]] constexpr std::uint32_t rgb(std::uint8_t r, std::uint8_t g,
                                                  std::uint8_t b) noexcept {
            return (static_cast<std::uint32_t>(r) << 16U) |
                   (static_cast<std::uint32_t>(g) << 8U) | b;
        }

        [[nodiscard]] constexpr std::uint8_t expand_3bit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((value & 0x07U) * 0x24U);
        }

        [[nodiscard]] constexpr std::uint8_t expand_2bit(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>((value & 0x03U) * 0x55U);
        }

        [[nodiscard]] std::uint32_t diagnostic_color(std::uint8_t value,
                                                     std::uint8_t mix) noexcept {
            const std::uint8_t r = static_cast<std::uint8_t>(expand_3bit(value) ^ mix);
            const std::uint8_t g =
                static_cast<std::uint8_t>(expand_3bit(value >> 3U) ^ (mix >> 1U));
            const std::uint8_t b =
                static_cast<std::uint8_t>(expand_2bit(value >> 6U) ^ (mix << 1U));
            return rgb(r, g, b);
        }
    } // namespace

    upd94244::upd94244()
        : pixels_(static_cast<std::size_t>(visible_width) * visible_height, 0U) {
        introspection_.with_registers([this] { return register_snapshot(); });
        reset(reset_kind::power_on);
    }

    chip_metadata upd94244::metadata() const noexcept {
        return {
            .manufacturer = "NEC",
            .part_number = "uPD94244-210",
            .family = "uPD94244",
            .klass = chip_class::video,
            .revision = 1U,
        };
    }

    void upd94244::tick(std::uint64_t cycles) { elapsed_cycles_ += cycles; }

    void upd94244::reset(reset_kind /*kind*/) {
        regs_.fill(0U);
        vram_.fill(0U);
        std::fill(pixels_.begin(), pixels_.end(), 0U);
        elapsed_cycles_ = 0U;
        frame_index_ = 0U;
    }

    frame_buffer_view upd94244::framebuffer() const noexcept {
        return {.pixels = pixels_.data(),
                .width = visible_width,
                .height = visible_height,
                .stride = visible_width};
    }

    void upd94244::write_register(std::uint8_t offset, std::uint32_t value) noexcept {
        regs_[offset % regs_.size()] = value;
    }

    std::uint32_t upd94244::read_register(std::uint8_t offset) const noexcept {
        return regs_[offset % regs_.size()];
    }

    void upd94244::write_vram(std::uint32_t offset, std::uint8_t value) noexcept {
        vram_[offset % vram_.size()] = value;
    }

    std::uint8_t upd94244::read_vram(std::uint32_t offset) const noexcept {
        return vram_[offset % vram_.size()];
    }

    void upd94244::compose_diagnostic(std::span<const std::uint8_t> main_rom,
                                      std::span<const std::uint8_t> ymz_rom,
                                      std::span<const std::uint8_t> work_ram,
                                      std::span<const std::uint8_t> nvram,
                                      std::uint8_t input_latch,
                                      std::uint8_t control_latch) {
        const std::uint8_t reg_mix =
            static_cast<std::uint8_t>(regs_[0] ^ (regs_[1] >> 8U) ^ (regs_[2] >> 16U));
        const std::uint8_t frame_mix =
            static_cast<std::uint8_t>(frame_index_ ^ input_latch ^ control_latch ^ reg_mix);
        for (std::uint32_t y = 0; y < visible_height; ++y) {
            for (std::uint32_t x = 0; x < visible_width; ++x) {
                const std::uint64_t linear = static_cast<std::uint64_t>(y) * visible_width + x;
                const std::uint8_t gfx = sample_byte(vdp_rom_, (linear >> 1U) + (x * 17U) + y,
                                                     static_cast<std::uint8_t>(x ^ y));
                const std::uint8_t program =
                    sample_byte(main_rom, (linear >> 3U) ^ frame_index_, gfx);
                const std::uint8_t sample =
                    sample_byte(ymz_rom, (linear >> 2U) + (y * 11U), program);
                const std::uint8_t vram =
                    this->vram_[static_cast<std::size_t>((linear + regs_[3]) % this->vram_.size())];
                const std::uint8_t work = sample_byte(work_ram, (linear >> 4U) + x, sample);
                const std::uint8_t nv =
                    sample_byte(nvram, (linear >> 5U) + y, static_cast<std::uint8_t>(~work));
                const std::uint8_t color =
                    static_cast<std::uint8_t>(gfx ^ program ^ sample ^ vram ^ work ^ nv ^
                                              frame_mix);
                pixels_[static_cast<std::size_t>(y) * visible_width + x] =
                    diagnostic_color(color, static_cast<std::uint8_t>(frame_mix + (x >> 4U)));
            }
        }
        ++frame_index_;
    }

    void upd94244::save_state(state_writer& writer) const {
        writer.u64(elapsed_cycles_);
        writer.u64(frame_index_);
        for (const std::uint32_t reg : regs_) {
            writer.u32(reg);
        }
        writer.bytes(vram_);
        for (const std::uint32_t pixel : pixels_) {
            writer.u32(pixel);
        }
    }

    void upd94244::load_state(state_reader& reader) {
        elapsed_cycles_ = reader.u64();
        frame_index_ = reader.u64();
        for (std::uint32_t& reg : regs_) {
            reg = reader.u32();
        }
        reader.bytes(vram_);
        for (std::uint32_t& pixel : pixels_) {
            pixel = reader.u32();
        }
    }

    instrumentation::ichip_introspection& upd94244::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> upd94244::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FRAME", frame_index_, 64U, fmt::unsigned_integer};
        register_view_[1] = {"ELAPSED", elapsed_cycles_, 64U, fmt::unsigned_integer};
        register_view_[2] = {"REG0", regs_[0], 32U, fmt::flags};
        register_view_[3] = {"REG1", regs_[1], 32U, fmt::flags};
        register_view_[4] = {"REG2", regs_[2], 32U, fmt::flags};
        register_view_[5] = {"REG3", regs_[3], 32U, fmt::flags};
        register_view_[6] = {"VRAM0", vram_[0], 8U, fmt::unsigned_integer};
        register_view_[7] = {"VRAM1", vram_[1], 8U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto upd94244_registration =
            register_factory("nec.upd94244", chip_class::video,
                             []() -> std::unique_ptr<ichip> {
                                 return std::make_unique<upd94244>();
                             });
    } // namespace

} // namespace mnemos::chips::video
