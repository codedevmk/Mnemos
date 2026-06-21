#include "n163.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        [[nodiscard]] std::int16_t clip16(int v) noexcept {
            return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
        }
    } // namespace

    chip_metadata n163::metadata() const noexcept {
        return {
            .manufacturer = "Namco",
            .part_number = "163",
            .family = "Namco 163",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    // Advance one channel's phase accumulator and resolve its 4-bit sample. The
    // phase, frequency and wave parameters live in the shared RAM (eight bytes from
    // base $40 + ch*8), so the update reads and writes RAM in place exactly as the
    // hardware does.
    void n163::service_channel(std::size_t ch) noexcept {
        const std::size_t b = 0x40U + ch * 8U;
        const std::uint32_t freq = static_cast<std::uint32_t>(ram_[b]) |
                                   (static_cast<std::uint32_t>(ram_[b + 2U]) << 8U) |
                                   (static_cast<std::uint32_t>(ram_[b + 4U] & 0x03U) << 16U);
        std::uint32_t phase = static_cast<std::uint32_t>(ram_[b + 1U]) |
                              (static_cast<std::uint32_t>(ram_[b + 3U]) << 8U) |
                              (static_cast<std::uint32_t>(ram_[b + 5U]) << 16U);
        // Wave length in 4-bit samples (4..256); the phase wraps at length<<16.
        const std::uint32_t length = 256U - static_cast<std::uint32_t>(ram_[b + 4U] & 0xFCU);
        const std::uint8_t wave_addr = ram_[b + 6U];
        const std::uint8_t volume = static_cast<std::uint8_t>(ram_[b + 7U] & 0x0FU);

        phase = (phase + freq) % (length << 16U);
        ram_[b + 1U] = static_cast<std::uint8_t>(phase & 0xFFU);
        ram_[b + 3U] = static_cast<std::uint8_t>((phase >> 8U) & 0xFFU);
        ram_[b + 5U] = static_cast<std::uint8_t>((phase >> 16U) & 0xFFU);

        const std::uint32_t sample_index = ((phase >> 16U) + wave_addr) & 0xFFU;
        const std::uint8_t byte = ram_[(sample_index >> 1U) & 0x7FU];
        const std::uint8_t sample =
            static_cast<std::uint8_t>((sample_index & 1U) != 0U ? (byte >> 4U) : (byte & 0x0FU));
        // Centre the unsigned 4-bit sample at zero, scale by the 4-bit volume.
        chan_out_[ch] = (static_cast<int>(sample) - 8) * static_cast<int>(volume);
    }

    void n163::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (enabled_ && ++cycle_div_ >= k_service_cycles) {
                cycle_div_ = 0;
                const std::size_t num_active = active_channels();
                // Service the active channels from the top of the bank downwards.
                const std::size_t ch = 7U - (service_idx_ % num_active);
                service_channel(ch);
                service_idx_ = (service_idx_ + 1U) % num_active;
            }
            if (++sample_prescaler_ >= clock_divider_) {
                sample_prescaler_ = 0;
                int scaled = 0;
                if (enabled_) {
                    const std::size_t num_active = active_channels();
                    int sum = 0;
                    for (std::size_t ch = 8U - num_active; ch < 8U; ++ch) {
                        sum += chan_out_[ch];
                    }
                    // The single output pin time-shares the active channels; the
                    // analog average is sum/N. Scale that into the int16 range.
                    scaled = sum * k_output_gain / static_cast<int>(num_active);
                }
                // One-pole DC blocker: AC-couple so a steady waveform / muted chip
                // settles to zero.
                const double raw = static_cast<double>(scaled);
                dc_ += 0.0008 * (raw - dc_);
                const std::int16_t s = clip16(static_cast<int>(raw - dc_));
                last_left_ = s;
                last_right_ = s;
                if (audio_capture_) {
                    sample_queue_.push_back(s);
                    sample_queue_.push_back(s);
                }
            }
        }
    }

    std::size_t n163::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void n163::reset(reset_kind /*kind*/) {
        ram_.fill(0U);
        addr_ = 0U;
        autoinc_ = false;
        enabled_ = true;
        cycle_div_ = 0;
        service_idx_ = 0U;
        chan_out_.fill(0);
        last_left_ = 0;
        last_right_ = 0;
        dc_ = 0.0;
        sample_prescaler_ = 0;
        sample_queue_.clear();
    }

    void n163::save_state(state_writer& writer) const {
        for (const std::uint8_t b : ram_) {
            writer.u8(b);
        }
        writer.u8(addr_);
        writer.boolean(autoinc_);
        writer.boolean(enabled_);
        writer.u32(static_cast<std::uint32_t>(cycle_div_));
        writer.u32(static_cast<std::uint32_t>(service_idx_));
        for (const std::int32_t o : chan_out_) {
            writer.u32(static_cast<std::uint32_t>(o));
        }
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(sample_prescaler_));
        writer.u64(std::bit_cast<std::uint64_t>(dc_)); // DC-blocker IIR state
    }

    void n163::load_state(state_reader& reader) {
        for (std::uint8_t& b : ram_) {
            b = reader.u8();
        }
        addr_ = reader.u8();
        autoinc_ = reader.boolean();
        enabled_ = reader.boolean();
        cycle_div_ = static_cast<int>(reader.u32());
        service_idx_ = static_cast<std::size_t>(reader.u32());
        for (std::int32_t& o : chan_out_) {
            o = static_cast<std::int32_t>(reader.u32());
        }
        clock_divider_ = static_cast<int>(reader.u32());
        sample_prescaler_ = static_cast<int>(reader.u32());
        dc_ = std::bit_cast<double>(reader.u64());
    }

    instrumentation::ichip_introspection& n163::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> n163::register_snapshot() noexcept {
        using fmt = register_value_format;
        const std::size_t b = 0x78U; // top channel (the first active)
        const std::uint32_t freq = static_cast<std::uint32_t>(ram_[b]) |
                                   (static_cast<std::uint32_t>(ram_[b + 2U]) << 8U) |
                                   (static_cast<std::uint32_t>(ram_[b + 4U] & 0x03U) << 16U);
        register_view_[0] = {"ADDR", addr_, 7U, fmt::unsigned_integer};
        register_view_[1] = {"AUTOINC", autoinc_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[2] = {"NUM_CH", static_cast<std::uint32_t>(active_channels()), 4U,
                             fmt::unsigned_integer};
        register_view_[3] = {"CH7_FREQ", freq, 18U, fmt::unsigned_integer};
        register_view_[4] = {"CH7_VOL", static_cast<std::uint32_t>(ram_[b + 7U] & 0x0FU), 4U,
                             fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto n163_registration =
            register_factory("namco.163", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<n163>(); });
    } // namespace

} // namespace mnemos::chips::audio
