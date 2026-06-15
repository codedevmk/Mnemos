#include "okim6295.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // 16-level attenuation, ~3 dB per step (out of 256); codes 9..15 mute.
        constexpr std::array<int, 16> volume_table = {256, 180, 128, 90, 64, 45, 32, 22,
                                                      16,  0,   0,   0,  0,  0,  0,  0};
        // Step-index movement keyed by the magnitude bits of the nibble.
        constexpr std::array<int, 8> index_adjust = {-1, -1, -1, -1, 2, 4, 6, 8};
        // The 49-entry ADPCM step ladder.
        constexpr std::array<int, okim6295::max_step_index + 1> step_table = {
            16,  17,  19,  21,  23,  25,  28,  31,  34,  37,  41,   45,   50,   55,   60,  66,  73,
            80,  88,  97,  107, 118, 130, 143, 157, 173, 185, 204,  224,  247,  272,  300, 330, 363,
            399, 439, 483, 532, 585, 622, 684, 752, 828, 910, 1002, 1102, 1212, 1333, 1466};

        // Apply one ADPCM nibble: update the predictor (clamped to the 12-bit
        // signed range) and the step-index (clamped to the ladder), in place.
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
            if (predictor > 2047) {
                predictor = 2047;
            } else if (predictor < -2048) {
                predictor = -2048;
            }
            step_index += index_adjust[nibble & 0x7U];
            if (step_index < 0) {
                step_index = 0;
            } else if (step_index > okim6295::max_step_index) {
                step_index = okim6295::max_step_index;
            }
        }

        // Decode one phrase's whole ADPCM stream (fresh predictor/step) into
        // 16-bit PCM, high nibble first, one sample per nibble. Cold-path export
        // only -- the live channels decode incrementally in okim6295::step().
        void decode_phrase(std::span<const std::uint8_t> rom, std::uint32_t start,
                           std::uint32_t end, std::vector<std::int16_t>& out) {
            std::int32_t predictor = 0;
            int step_index = 0;
            std::uint32_t addr = start;
            bool high = true;
            while (addr <= end && addr < rom.size()) {
                const std::uint8_t byte_val = rom[addr];
                std::uint8_t nibble;
                if (high) {
                    nibble = static_cast<std::uint8_t>((byte_val >> 4U) & 0x0FU);
                    high = false;
                } else {
                    nibble = static_cast<std::uint8_t>(byte_val & 0x0FU);
                    high = true;
                    ++addr;
                }
                adpcm_apply(nibble, predictor, step_index);
                out.push_back(static_cast<std::int16_t>(predictor << 4));
            }
        }
    } // namespace

    chip_metadata okim6295::metadata() const noexcept {
        return {
            .manufacturer = "OKI",
            .part_number = "MSM6295",
            .family = "ADPCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void okim6295::start_channel(channel& ch, std::uint32_t start_addr, std::uint32_t end_addr,
                                 std::uint8_t volume_code) noexcept {
        if (ch.active) {
            return; // busy gating: a sounding channel ignores a retrigger
        }
        ch.active = true;
        ch.current_addr = start_addr;
        ch.end_addr = end_addr;
        ch.nibble_high = true;
        ch.predictor = 0;
        ch.step_index = 0;
        ch.volume_code = static_cast<std::uint8_t>(volume_code & 0x0FU);
        ch.volume = volume_table[ch.volume_code];
    }

    void okim6295::write_command(std::uint8_t data) noexcept {
        if (reg_write_callback_) {
            // The MSM6295 has a single command port; log the raw command stream.
            reg_write_callback_({.port = 0U, .value = data});
        }
        if (!command_pending_) {
            if ((data & 0x80U) != 0U) {
                // Play command byte 1: 1xxxxxxx selects the phrase.
                pending_phrase_ = static_cast<std::uint8_t>(data & 0x7FU);
                command_pending_ = true;
            } else {
                // Stop command: bits 3..6 stop channels 0..3.
                if ((data & 0x08U) != 0U) {
                    channels_[0].active = false;
                }
                if ((data & 0x10U) != 0U) {
                    channels_[1].active = false;
                }
                if ((data & 0x20U) != 0U) {
                    channels_[2].active = false;
                }
                if ((data & 0x40U) != 0U) {
                    channels_[3].active = false;
                }
            }
            return;
        }

        // Play command byte 2: high nibble is the channel mask, low nibble the
        // attenuation code.
        const std::uint8_t channel_mask = static_cast<std::uint8_t>((data >> 4U) & 0x0FU);
        const std::uint8_t volume_code = static_cast<std::uint8_t>(data & 0x0FU);
        command_pending_ = false;

        const std::uint32_t phrase = pending_phrase_;
        if (rom_.empty() || (phrase * 8U + 5U) >= rom_.size()) {
            return;
        }
        const std::uint32_t start_addr =
            (static_cast<std::uint32_t>(rom_[phrase * 8U + 0U]) << 16U) |
            (static_cast<std::uint32_t>(rom_[phrase * 8U + 1U]) << 8U) |
            static_cast<std::uint32_t>(rom_[phrase * 8U + 2U]);
        const std::uint32_t end_addr = (static_cast<std::uint32_t>(rom_[phrase * 8U + 3U]) << 16U) |
                                       (static_cast<std::uint32_t>(rom_[phrase * 8U + 4U]) << 8U) |
                                       static_cast<std::uint32_t>(rom_[phrase * 8U + 5U]);
        if (start_addr >= end_addr || end_addr >= rom_.size()) {
            return;
        }
        if ((channel_mask & 0x1U) != 0U) {
            start_channel(channels_[0], start_addr, end_addr, volume_code);
        }
        if ((channel_mask & 0x2U) != 0U) {
            start_channel(channels_[1], start_addr, end_addr, volume_code);
        }
        if ((channel_mask & 0x4U) != 0U) {
            start_channel(channels_[2], start_addr, end_addr, volume_code);
        }
        if ((channel_mask & 0x8U) != 0U) {
            start_channel(channels_[3], start_addr, end_addr, volume_code);
        }
    }

    std::uint8_t okim6295::read_status() const noexcept {
        std::uint8_t status = 0xF0U;
        for (int i = 0; i < channel_count; ++i) {
            if (channels_[static_cast<std::size_t>(i)].active) {
                status |= static_cast<std::uint8_t>(1U << i);
            }
        }
        return status;
    }

    std::int16_t okim6295::step() noexcept {
        std::int32_t mixed = 0;
        for (channel& ch : channels_) {
            if (!ch.active) {
                continue;
            }
            if (rom_.empty() || ch.current_addr >= rom_.size()) {
                ch.active = false;
                continue;
            }

            const std::uint8_t byte_val = rom_[ch.current_addr];
            std::uint8_t nibble;
            if (ch.nibble_high) {
                nibble = static_cast<std::uint8_t>((byte_val >> 4U) & 0x0FU);
                ch.nibble_high = false;
            } else {
                nibble = static_cast<std::uint8_t>(byte_val & 0x0FU);
                ch.nibble_high = true;
                ++ch.current_addr;
            }

            adpcm_apply(nibble, ch.predictor, ch.step_index);

            if (ch.current_addr > ch.end_addr) {
                ch.active = false;
            }

            // 12-bit predictor up to 16-bit, then apply the attenuation factor.
            const std::int32_t sample16 = ch.predictor << 4;
            mixed += (sample16 * ch.volume) >> 8;
        }

        if (mixed > 32767) {
            mixed = 32767;
        } else if (mixed < -32768) {
            mixed = -32768;
        }
        last_sample_ = static_cast<std::int16_t>(mixed);
        return last_sample_;
    }

    void okim6295::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            const std::int16_t s = step();
            buf_lr[i * 2U] = s;
            buf_lr[i * 2U + 1U] = s;
        }
    }

    void okim6295::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (++prescaler_ >= clock_divider_) {
                prescaler_ = 0;
                step();
                if (audio_capture_) {
                    sample_queue_.push_back(last_sample_);
                    sample_queue_.push_back(last_sample_);
                }
            }
        }
    }

    std::size_t okim6295::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void okim6295::reset(reset_kind /*kind*/) {
        // No power-on-only state: the sample ROM is host-owned (never cleared
        // here), so every reset kind clears the same playback/command state.
        for (channel& ch : channels_) {
            ch = channel{};
        }
        command_pending_ = false;
        pending_phrase_ = 0U;
        last_sample_ = 0;
        prescaler_ = 0;
        sample_queue_.clear();
    }

    void okim6295::save_state(state_writer& writer) const {
        for (const auto& ch : channels_) {
            writer.boolean(ch.active);
            writer.u32(ch.current_addr);
            writer.u32(ch.end_addr);
            writer.boolean(ch.nibble_high);
            writer.u32(static_cast<std::uint32_t>(ch.predictor));
            writer.u32(static_cast<std::uint32_t>(ch.step_index));
            writer.u8(ch.volume_code);
            writer.u32(static_cast<std::uint32_t>(ch.volume));
        }
        writer.boolean(command_pending_);
        writer.u8(pending_phrase_);
        writer.u16(static_cast<std::uint16_t>(last_sample_));
        writer.boolean(pin7_high_);
        writer.u32(input_clock_hz_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
    }

    void okim6295::load_state(state_reader& reader) {
        // Clamp decoded values to their valid ranges (mirroring the live setters):
        // a corrupt or forward-incompatible blob must not yield an out-of-ladder
        // step index (OOB read), a predictor that overflows in adpcm_apply, a
        // volume that overflows the mix, or (below) a 0 divider that makes tick()
        // step every cycle.
        for (auto& ch : channels_) {
            ch.active = reader.boolean();
            ch.current_addr = reader.u32();
            ch.end_addr = reader.u32();
            ch.nibble_high = reader.boolean();
            ch.predictor = std::clamp(static_cast<std::int32_t>(reader.u32()), -2048, 2047);
            ch.step_index = std::clamp(static_cast<int>(reader.u32()), 0, max_step_index);
            ch.volume_code = reader.u8();
            ch.volume = std::clamp(static_cast<int>(reader.u32()), 0, 256);
        }
        command_pending_ = reader.boolean();
        pending_phrase_ = reader.u8();
        last_sample_ = static_cast<std::int16_t>(reader.u16());
        pin7_high_ = reader.boolean();
        input_clock_hz_ = reader.u32();
        const int loaded_divider = static_cast<int>(reader.u32());
        clock_divider_ = loaded_divider > 0 ? loaded_divider : default_clock_divider;
        prescaler_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& okim6295::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> okim6295::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"STATUS", read_status(), 8U, fmt::flags};
        register_view_[1] = {"CMDPEND", command_pending_ ? 1ULL : 0ULL, 1U, fmt::flags};
        register_view_[2] = {"PHRASE", pending_phrase_, 7U, fmt::unsigned_integer};
        register_view_[3] = {"CH0ADDR", channels_[0].current_addr, 24U, fmt::unsigned_integer};
        register_view_[4] = {"CH1ADDR", channels_[1].current_addr, 24U, fmt::unsigned_integer};
        register_view_[5] = {"CH2ADDR", channels_[2].current_addr, 24U, fmt::unsigned_integer};
        register_view_[6] = {"CH3ADDR", channels_[3].current_addr, 24U, fmt::unsigned_integer};
        register_view_[7] = {"CH0STEP", static_cast<std::uint64_t>(channels_[0].step_index), 8U,
                             fmt::unsigned_integer};
        return register_view_;
    }

    std::span<const instrumentation::sample_view> okim6295::audio_source_impl::samples() const {
        const auto rom = owner_->rom_;
        const std::uint32_t rate = owner_->native_sample_rate();

        struct meta final {
            std::uint32_t phrase;
            std::uint32_t start;
            std::size_t off;
            std::size_t len;
        };
        std::vector<meta> metas;

        pcm_.clear();
        // Decode every in-range phrase-table entry (8 bytes/phrase, 24-bit BE
        // start/end). Empty (start >= end) and out-of-range entries are skipped,
        // so the unused/zero tail of the table contributes nothing.
        for (std::uint32_t phrase = 0; phrase < 128U; ++phrase) {
            const std::uint32_t base = phrase * 8U;
            if (rom.empty() || base + 5U >= rom.size()) {
                break;
            }
            const std::uint32_t start = (static_cast<std::uint32_t>(rom[base + 0U]) << 16U) |
                                        (static_cast<std::uint32_t>(rom[base + 1U]) << 8U) |
                                        static_cast<std::uint32_t>(rom[base + 2U]);
            const std::uint32_t end = (static_cast<std::uint32_t>(rom[base + 3U]) << 16U) |
                                      (static_cast<std::uint32_t>(rom[base + 4U]) << 8U) |
                                      static_cast<std::uint32_t>(rom[base + 5U]);
            if (start >= end || end >= rom.size()) {
                continue;
            }
            const std::size_t off = pcm_.size();
            decode_phrase(rom, start, end, pcm_);
            metas.push_back(
                {.phrase = phrase, .start = start, .off = off, .len = pcm_.size() - off});
        }

        // names_ is reserved up front so the string_views the samples hold into it
        // never dangle on reallocation; pcm_ is complete, so its spans are stable.
        names_.clear();
        names_.reserve(metas.size());
        samples_.clear();
        samples_.reserve(metas.size());
        for (std::size_t i = 0; i < metas.size(); ++i) {
            std::array<char, 16> buf{};
            std::snprintf(buf.data(), buf.size(), "phrase_%02x",
                          static_cast<unsigned>(metas[i].phrase));
            names_.emplace_back(buf.data());
            samples_.push_back(instrumentation::sample_view{
                .name = names_[i],
                .frames = std::span<const std::int16_t>(pcm_).subspan(metas[i].off, metas[i].len),
                .sample_rate = rate,
                .channels = 1,
                .loop_start = -1,
                .source_addr = metas[i].start});
        }
        return samples_;
    }

    namespace {
        [[maybe_unused]] const auto okim6295_registration = register_factory(
            "oki.msm6295", chip_class::audio_synth,
            []() -> std::unique_ptr<ichip> { return std::make_unique<okim6295>(); });
    } // namespace

} // namespace mnemos::chips::audio
