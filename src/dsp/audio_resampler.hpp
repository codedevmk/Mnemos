#pragma once

// Audio-path math shared by every player_system adapter that mixes one or
// more sound chips into the SDL output stream. Pure DSP -- no per-system
// state, no manifest knowledge. The system-specific bits (FM/PSG gain
// constants, the actual mixer routine) stay in each adapter.

#include <cstdint>

namespace mnemos::dsp {

    // Common mixer fixed-point: Q12 for gain math (1.0 == 4096). Adapters
    // multiply their per-channel sample by a Q12 gain via `scale_q12`, sum
    // the results, then clip to int16 via `clip_i16`.
    inline constexpr int kMixerGainShift = 12;
    inline constexpr int kMixerGainOne = 1 << kMixerGainShift;

    // Output sample rate the player streams to SDL. Each adapter resamples
    // its native rates to this; SDL's audio stream handles the final
    // host-device-rate adjustment.
    inline constexpr std::uint32_t kOutputRate = 48000U;

    // Clip an int to int16 with saturation, no UB on overflow.
    [[nodiscard]] inline std::int16_t clip_i16(int v) noexcept {
        if (v > 32767) {
            return 32767;
        }
        if (v < -32768) {
            return -32768;
        }
        return static_cast<std::int16_t>(v);
    }

    // Apply a Q12 gain to `sample` with rounding-half-away-from-zero so the
    // adapter doesn't lose 1 LSB per scaled sample to truncation.
    [[nodiscard]] inline int scale_q12(int sample, int gain_q12) noexcept {
        int scaled = sample * gain_q12;
        scaled += scaled >= 0 ? (kMixerGainOne / 2) : -(kMixerGainOne / 2);
        return scaled / kMixerGainOne;
    }

    // Linear-interpolated sample lookup at a fractional source position.
    // `src` is interleaved with `stride` int16s per sample frame; `channel`
    // selects the channel within the frame (0 for mono, 0/1 for stereo).
    // `src_count` is the number of frames available. Used for upsampling
    // (when the source rate is below the destination rate).
    [[nodiscard]] inline int sample_channel_linear(const std::int16_t* src, int stride, int channel,
                                                   int src_count, double pos) noexcept {
        if (!src || src_count <= 0) {
            return 0;
        }
        if (src_count == 1 || pos <= 0.0) {
            return src[channel];
        }
        int left_index = static_cast<int>(pos);
        if (left_index >= src_count - 1) {
            return src[(src_count - 1) * stride + channel];
        }
        int right_index = left_index + 1;
        double frac = pos - static_cast<double>(left_index);
        double a = static_cast<double>(src[left_index * stride + channel]);
        double b = static_cast<double>(src[right_index * stride + channel]);
        return static_cast<int>(a + (b - a) * frac);
    }

    // Box-average sample lookup over [start, end) of source frame indices.
    // Used for downsampling (when the source rate exceeds the destination
    // rate) so we don't alias by point-sampling. Degenerate ranges fall
    // back to `sample_channel_linear` at the start position.
    [[nodiscard]] inline int sample_channel_box(const std::int16_t* src, int stride, int channel,
                                                int src_count, double start, double end) noexcept {
        if (!src || src_count <= 0) {
            return 0;
        }
        if (end <= start) {
            return sample_channel_linear(src, stride, channel, src_count, start);
        }
        if (start < 0.0) {
            start = 0.0;
        }
        if (end > static_cast<double>(src_count)) {
            end = static_cast<double>(src_count);
        }
        int first = static_cast<int>(start);
        int last = static_cast<int>(end);
        if (last >= src_count) {
            last = src_count - 1;
        }
        double accum = 0.0;
        double total = 0.0;
        for (int i = first; i <= last; ++i) {
            double seg_start = start > static_cast<double>(i) ? start : static_cast<double>(i);
            double seg_end = end < static_cast<double>(i + 1) ? end : static_cast<double>(i + 1);
            double w = seg_end - seg_start;
            if (w <= 0.0) {
                continue;
            }
            accum += static_cast<double>(src[i * stride + channel]) * w;
            total += w;
        }
        if (total <= 0.0) {
            return sample_channel_linear(src, stride, channel, src_count, start);
        }
        return static_cast<int>(accum / total);
    }

} // namespace mnemos::dsp
