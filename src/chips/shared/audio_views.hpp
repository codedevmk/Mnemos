#pragma once

// The audio SAMPLE-extraction contract: PCM waveforms a chip can surface for
// ripping tools. Tier 2: declared here so tier-2 chips (a PCM sampler like the
// RF5C68) can decode their native sample memory into this neutral type;
// consumed by tier 6+ (the audio exporter).
//
// This is the audio analogue of asset_views.hpp's `asset_source`, and is
// deliberately narrow:
//   * It carries SAMPLED waveforms only (stored PCM, e.g. RF5C68 wave RAM).
//   * It does NOT carry synth voice/instrument state -- that is already exposed
//     as `register_view` (introspection_views.hpp); the exporter serialises a
//     chip's register_snapshot() directly rather than duplicating it here.
//   * It does NOT capture temporal streams (an FM/DAC mixdown over time). That
//     is a trace-shaped, over-time concern (cf. `trace_target`), not a snapshot,
//     and is out of scope for this contract.
//
// Like the graphics capabilities, `audio_source` is OPTIONAL: a chip with no
// extractable samples returns nullptr from `ichip_introspection::audio()`. The
// spans handed out are owned by the chip and valid only until its next tick
// (the same lifetime rule as memory_view); a consumer that retains them copies.

#include <cstdint>
#include <span>
#include <string_view>

namespace mnemos::instrumentation {

    // One PCM waveform decoded to signed 16-bit frames. `frames` is interleaved
    // when `channels > 1`; its length is `frame_count * channels`. `loop_start`
    // is the frame index the hardware loops back to, or -1 for a one-shot.
    // `source_addr` is the chip-relative address the sample was decoded from
    // (e.g. a wave-RAM offset), for cross-referencing a memory_view dump.
    struct sample_view final {
        std::string_view name;
        std::span<const std::int16_t> frames;
        std::uint32_t sample_rate{};
        std::uint8_t channels{1};
        int loop_start{-1};
        std::uint32_t source_addr{};

        // Frames per channel implied by the span and channel count.
        [[nodiscard]] constexpr std::uint32_t frame_count() const noexcept {
            return channels == 0U ? 0U : static_cast<std::uint32_t>(frames.size() / channels);
        }
        // True when the span length is a whole number of channel-interleaved
        // frames (and the channel count is sane).
        [[nodiscard]] constexpr bool well_formed() const noexcept {
            return channels != 0U && (frames.size() % channels) == 0U;
        }
    };

    // The optional capability returned by `ichip_introspection::audio()`. A chip
    // decodes its current sample memory into a flat list of waveforms. The span,
    // and everything it points at, follows the tick lifetime rule above.
    class audio_source {
      public:
        audio_source() = default;
        audio_source(const audio_source&) = delete;
        audio_source& operator=(const audio_source&) = delete;
        virtual ~audio_source() = default;

        [[nodiscard]] virtual std::span<const sample_view> samples() const = 0;
    };

} // namespace mnemos::instrumentation
