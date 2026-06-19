#include "ula.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <cstring>
#include <memory>

namespace mnemos::chips::video {

    namespace {
        // 8 colours x {normal, bright}, as 0x00RRGGBB. Normal uses $C0, bright $FF.
        constexpr std::array<std::array<std::uint32_t, 8>, 2> k_palette = {{
            {0x000000U, 0x0000C0U, 0xC00000U, 0xC000C0U, 0x00C000U, 0x00C0C0U, 0xC0C000U,
             0xC0C0C0U},
            {0x000000U, 0x0000FFU, 0xFF0000U, 0xFF00FFU, 0x00FF00U, 0x00FFFFU, 0xFFFF00U,
             0xFFFFFFU},
        }};

        constexpr std::uint32_t k_state_version = 1U;

        // Bitmap byte offset from the $4000 screen base for the 8-pixel cell at
        // (x, y): the Spectrum's interleaved third/character/scanline layout.
        [[nodiscard]] constexpr std::size_t bitmap_offset(int x, int y) noexcept {
            return static_cast<std::size_t>(((y & 0xC0) << 5) | ((y & 0x07) << 8) |
                                            ((y & 0x38) << 2) | (x >> 3));
        }
        // Attribute byte offset from the $4000 base ($5800 region).
        [[nodiscard]] constexpr std::size_t attr_offset(int x, int y) noexcept {
            return static_cast<std::size_t>(0x1800 + ((y >> 3) * 32) + (x >> 3));
        }
    } // namespace

    chip_metadata ula::metadata() const noexcept {
        return {.manufacturer = "Sinclair",
                .part_number = "ULA",
                .family = "ZX Spectrum",
                .klass = chip_class::video,
                .revision = 1U};
    }

    void ula::render_frame() noexcept {
        const std::uint32_t border_rgb = k_palette[0][border_ & 0x07U];
        for (std::uint32_t& px : framebuffer_) {
            px = border_rgb;
        }
        if (screen_ram_.size() < screen_ram_bytes) {
            return; // no screen attached -- border only
        }

        const bool flash_phase = (frame_count_ & 0x10U) != 0U;
        for (int y = 0; y < display_height; ++y) {
            std::uint32_t* row =
                framebuffer_.data() + static_cast<std::size_t>(y + screen_y_offset) * frame_width;
            for (int x = 0; x < display_width; x += 8) {
                const std::uint8_t bitmap = screen_ram_[bitmap_offset(x, y)];
                const std::uint8_t attr = screen_ram_[attr_offset(x, y)];
                std::uint8_t ink = attr & 0x07U;
                std::uint8_t paper = (attr >> 3U) & 0x07U;
                const std::size_t bright = (attr & 0x40U) != 0U ? 1U : 0U;
                if ((attr & 0x80U) != 0U && flash_phase) {
                    const std::uint8_t tmp = ink;
                    ink = paper;
                    paper = tmp;
                }
                for (int bit = 0; bit < 8; ++bit) {
                    const std::uint8_t color = (bitmap & (0x80U >> bit)) != 0U ? ink : paper;
                    row[static_cast<std::size_t>(screen_x_offset + x + bit)] =
                        k_palette[bright][color];
                }
            }
        }
    }

    void ula::drive_irq(bool asserted) noexcept {
        if (asserted == irq_asserted_) {
            return;
        }
        irq_asserted_ = asserted;
        if (irq_callback_) {
            irq_callback_(asserted);
        }
    }

    void ula::tick(std::uint64_t cycles) {
        auto remaining = static_cast<std::int64_t>(cycles);
        while (remaining > 0) {
            const int frame_left = tstates_per_frame - frame_tstates_;
            const int chunk = remaining < frame_left ? static_cast<int>(remaining) : frame_left;

            if (irq_pulse_ > 0) {
                irq_pulse_ = chunk >= irq_pulse_ ? 0 : irq_pulse_ - chunk;
            }

            frame_tstates_ += chunk;
            remaining -= chunk;

            if (frame_tstates_ >= tstates_per_frame) {
                frame_tstates_ = 0;
                ++frame_count_;
                render_frame();
                ++frame_index_;
                irq_pulse_ = irq_pulse_tstates; // /INT pulse at frame start
            }
            drive_irq(irq_pulse_ > 0);
        }
    }

    void ula::reset(reset_kind /*kind*/) {
        border_ = 0;
        frame_tstates_ = 0;
        irq_pulse_ = 0;
        frame_count_ = 0;
        frame_index_ = 0;
        std::memset(framebuffer_.data(), 0, framebuffer_.size() * sizeof(std::uint32_t));
        drive_irq(false);
    }

    frame_buffer_view ula::framebuffer() const noexcept {
        return {.pixels = framebuffer_.data(),
                .width = static_cast<std::uint32_t>(frame_width),
                .height = static_cast<std::uint32_t>(frame_height),
                .stride = 0U};
    }

    void ula::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        writer.u8(border_);
        writer.u32(static_cast<std::uint32_t>(frame_tstates_));
        writer.u32(static_cast<std::uint32_t>(irq_pulse_));
        writer.u32(frame_count_);
        writer.u64(frame_index_);
        writer.boolean(irq_asserted_);
    }

    void ula::load_state(state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        border_ = reader.u8();
        frame_tstates_ = static_cast<int>(reader.u32());
        irq_pulse_ = static_cast<int>(reader.u32());
        frame_count_ = reader.u32();
        frame_index_ = reader.u64();
        irq_asserted_ = reader.boolean();
    }

    namespace {
        [[maybe_unused]] const auto ula_registration =
            register_factory("sinclair.ula", chip_class::video,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ula>(); });
    } // namespace

} // namespace mnemos::chips::video
