#include "msm5205.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        constexpr std::array<int, 8> index_adjust = {-1, -1, -1, -1, 2, 4, 6, 8};
        constexpr std::array<int, msm5205::max_step_index + 1> step_table = {
            16,  17,  19,  21,  23,  25,  28,  31,  34,  37,  41,   45,   50,   55,   60,  66,  73,
            80,  88,  97,  107, 118, 130, 143, 157, 173, 185, 204,  224,  247,  272,  300, 330, 363,
            399, 439, 483, 532, 585, 622, 684, 752, 828, 910, 1002, 1102, 1212, 1333, 1466};

        void adpcm_apply(std::uint8_t nibble, std::int32_t& predictor, int& step_index) noexcept {
            const std::int32_t step_size = step_table[static_cast<std::size_t>(step_index)];
            std::int32_t delta = step_size / 8;
            if ((nibble & 0x4U) != 0U) {
                delta += step_size;
            }
            if ((nibble & 0x2U) != 0U) {
                delta += step_size / 2;
            }
            if ((nibble & 0x1U) != 0U) {
                delta += step_size / 4;
            }
            if ((nibble & 0x8U) != 0U) {
                predictor -= delta;
            } else {
                predictor += delta;
            }
            predictor = std::clamp(predictor, -2048, 2047);
            step_index += index_adjust[nibble & 0x7U];
            step_index = std::clamp(step_index, 0, msm5205::max_step_index);
        }
    } // namespace

    chip_metadata msm5205::metadata() const noexcept {
        return {.manufacturer = "OKI",
                .part_number = "MSM5205",
                .family = "ADPCM",
                .klass = chip_class::audio_synth,
                .revision = 1U};
    }

    void msm5205::data_w(std::uint8_t nibble) noexcept {
        data_latch_ = static_cast<std::uint8_t>(nibble & 0x0FU);
        note_write(0U, data_latch_);
    }

    void msm5205::reset_w(bool asserted) noexcept {
        reset_asserted_ = asserted;
        note_write(1U, asserted ? 1U : 0U);
        if (asserted) {
            predictor_ = 0;
            step_index_ = 0;
            last_sample_ = 0;
            prescaler_ = 0;
        }
    }

    void msm5205::vclk_tick() noexcept {
        if (reset_asserted_) {
            last_sample_ = 0;
            return;
        }
        adpcm_apply(data_latch_, predictor_, step_index_);
        last_sample_ = static_cast<std::int16_t>(predictor_ << 4);
        ++vclk_count_;
    }

    void msm5205::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            vclk_tick();
            buf_lr[i * 2U] = last_sample_;
            buf_lr[i * 2U + 1U] = last_sample_;
        }
    }

    void msm5205::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (++prescaler_ >= clock_divider_) {
                prescaler_ = 0;
                vclk_tick();
                if (audio_capture_) {
                    sample_queue_.push_back(last_sample_);
                    sample_queue_.push_back(last_sample_);
                }
            }
        }
    }

    std::size_t msm5205::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
        const std::size_t avail_pairs = sample_queue_.size() / 2U;
        const std::size_t n = std::min(avail_pairs, max_pairs);
        if (n == 0U) {
            return 0U;
        }
        std::memcpy(out, sample_queue_.data(), n * 2U * sizeof(std::int16_t));
        sample_queue_.erase(sample_queue_.begin(),
                            sample_queue_.begin() + static_cast<std::ptrdiff_t>(n * 2U));
        return n;
    }

    void msm5205::reset(reset_kind /*kind*/) {
        data_latch_ = 0U;
        reset_asserted_ = false;
        predictor_ = 0;
        step_index_ = 0;
        last_sample_ = 0;
        vclk_count_ = 0U;
        prescaler_ = 0;
        sample_queue_.clear();
    }

    void msm5205::save_state(state_writer& writer) const {
        writer.u8(data_latch_);
        writer.boolean(reset_asserted_);
        writer.u32(static_cast<std::uint32_t>(predictor_));
        writer.u32(static_cast<std::uint32_t>(step_index_));
        writer.u16(static_cast<std::uint16_t>(last_sample_));
        writer.u64(vclk_count_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
    }

    void msm5205::load_state(state_reader& reader) {
        data_latch_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        reset_asserted_ = reader.boolean();
        predictor_ = std::clamp(static_cast<std::int32_t>(reader.u32()), -2048, 2047);
        step_index_ = std::clamp(static_cast<int>(reader.u32()), 0, max_step_index);
        last_sample_ = static_cast<std::int16_t>(reader.u16());
        vclk_count_ = reader.u64();
        const int loaded_divider = static_cast<int>(reader.u32());
        clock_divider_ = loaded_divider > 0 ? loaded_divider : 1;
        prescaler_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& msm5205::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> msm5205::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"DATA", data_latch_, 4U, fmt::unsigned_integer};
        register_view_[1] = {"RESET", reset_asserted_ ? 1ULL : 0ULL, 1U, fmt::flags};
        register_view_[2] = {"PRED", static_cast<std::uint64_t>(predictor_ & 0xFFF), 12U,
                             fmt::signed_integer};
        register_view_[3] = {"STEP", static_cast<std::uint64_t>(step_index_), 8U,
                             fmt::unsigned_integer};
        register_view_[4] = {"LAST",
                             static_cast<std::uint64_t>(static_cast<std::uint16_t>(last_sample_)),
                             16U, fmt::signed_integer};
        register_view_[5] = {"VCLK", vclk_count_, 32U, fmt::unsigned_integer};
        register_view_[6] = {"DIV", static_cast<std::uint64_t>(clock_divider_), 16U,
                             fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto msm5205_registration = register_factory(
            "oki.msm5205", chip_class::audio_synth,
            []() -> std::unique_ptr<ichip> { return std::make_unique<msm5205>(); });
    } // namespace

} // namespace mnemos::chips::audio
