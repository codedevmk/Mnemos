#include "beeper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        constexpr std::uint32_t k_state_version = 1U;
    } // namespace

    chip_metadata beeper::metadata() const noexcept {
        return {.manufacturer = "generic",
                .part_number = "beeper",
                .family = "1-bit speaker",
                .klass = chip_class::audio_synth,
                .revision = 1U};
    }

    void beeper::tick(std::uint64_t cycles) {
        if (!audio_capture_ || cycles == 0U) {
            return;
        }
        const auto c = static_cast<std::int64_t>(cycles);
        if (speaker_high_) {
            level_sum_ += c;
        }
        window_ += c;
        phase_ += static_cast<std::int64_t>(output_rate_) * c;
        while (phase_ >= static_cast<std::int64_t>(cpu_clock_)) {
            phase_ -= static_cast<std::int64_t>(cpu_clock_);
            // Box-average the speaker level over the window and centre it: all-low
            // -> -amplitude, all-high -> +amplitude.
            std::int32_t sample = 0;
            if (window_ > 0) {
                const std::int64_t centred = (level_sum_ * 2) - window_;
                sample = static_cast<std::int32_t>((centred * amplitude_) / window_);
            }
            queue_.push_back(
                static_cast<std::int16_t>(std::clamp<std::int32_t>(sample, -32768, 32767)));
            level_sum_ = 0;
            window_ = 0;
        }
    }

    void beeper::reset(reset_kind /*kind*/) {
        speaker_high_ = false;
        level_sum_ = 0;
        window_ = 0;
        phase_ = 0;
        queue_.clear();
    }

    std::size_t beeper::drain_samples(std::int16_t* out, std::size_t max_samples) noexcept {
        const std::size_t n = std::min(max_samples, queue_.size());
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = queue_.front();
            queue_.pop_front();
        }
        return n;
    }

    void beeper::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        writer.boolean(speaker_high_);
        writer.u32(static_cast<std::uint32_t>(level_sum_));
        writer.u32(static_cast<std::uint32_t>(window_));
        writer.u32(static_cast<std::uint32_t>(phase_));
    }

    void beeper::load_state(state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        speaker_high_ = reader.boolean();
        level_sum_ = reader.u32();
        window_ = reader.u32();
        phase_ = reader.u32();
        queue_.clear();
    }

    namespace {
        [[maybe_unused]] const auto beeper_registration =
            register_factory("generic.beeper", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<beeper>(); });
    } // namespace

} // namespace mnemos::chips::audio
