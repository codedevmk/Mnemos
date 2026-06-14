#include "ricoh_2a03_apu.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // 5-bit length-counter index -> length value (the silicon length table;
        // ported integer-exact from the reference).
        constexpr std::array<std::uint8_t, 32> k_length_table = {
            10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
            12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

        // 4-bit period index -> noise timer period (the NTSC noise period table).
        constexpr std::array<std::uint16_t, 16> k_noise_period = {
            4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};

        // Per-duty 8-step square sequence (1 = high). Indexed [duty][step].
        constexpr std::array<std::array<std::uint8_t, 8>, 4> k_duty_table = {{
            {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
            {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
            {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
            {1, 0, 0, 1, 1, 1, 1, 1}, // 25% negated
        }};

        // 32-step triangle sequence (15..0,0..15).
        constexpr std::array<std::uint8_t, 32> k_triangle_table = {
            15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,  4,  3,  2,  1,  0,
            0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

        // Frame-counter step CPU cycles (NTSC 2A03), ported integer-exact.
        constexpr std::uint64_t k_frame_4step_last_cyc = 29829U;
        constexpr std::uint64_t k_frame_5step_last_cyc = 37281U;

        // Per-channel peak amplitude. The four square-ish channels each contribute
        // up to ~7 bits of headroom so the summed mono mix stays inside int16.
        constexpr std::int32_t k_channel_peak = 1500;

        [[nodiscard]] std::int32_t clamp16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return v;
        }
    } // namespace

    chip_metadata ricoh_2a03_apu::metadata() const noexcept {
        return {
            .manufacturer = "Ricoh",
            .part_number = "2A03",
            .family = "APU",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    // ---- register decode (ported integer-exact from the reference) ----

    void ricoh_2a03_apu::pulse_decode(pulse_channel& p) noexcept {
        p.duty = static_cast<std::uint8_t>((p.r0 >> 6U) & 0x03U);
        p.length_halt = (p.r0 & 0x20U) != 0U;
        p.constant_volume = (p.r0 & 0x10U) != 0U;
        p.volume_env = static_cast<std::uint8_t>(p.r0 & 0x0FU);

        p.sweep_enable = (p.r1 & 0x80U) != 0U;
        p.sweep_period = static_cast<std::uint8_t>((p.r1 >> 4U) & 0x07U);
        p.sweep_negate = (p.r1 & 0x08U) != 0U;
        p.sweep_shift = static_cast<std::uint8_t>(p.r1 & 0x07U);

        p.timer = static_cast<std::uint16_t>((p.r2 & 0xFFU) |
                                             (static_cast<std::uint16_t>(p.r3 & 0x07U) << 8U));
        p.length_load = static_cast<std::uint8_t>((p.r3 >> 3U) & 0x1FU);
    }

    void ricoh_2a03_apu::triangle_decode(triangle_channel& t) noexcept {
        t.length_halt = (t.r0 & 0x80U) != 0U;
        t.linear_reload = static_cast<std::uint8_t>(t.r0 & 0x7FU);
        t.timer = static_cast<std::uint16_t>((t.r2 & 0xFFU) |
                                             (static_cast<std::uint16_t>(t.r3 & 0x07U) << 8U));
        t.length_load = static_cast<std::uint8_t>((t.r3 >> 3U) & 0x1FU);
    }

    void ricoh_2a03_apu::noise_decode(noise_channel& n) noexcept {
        n.length_halt = (n.r0 & 0x20U) != 0U;
        n.constant_volume = (n.r0 & 0x10U) != 0U;
        n.volume_env = static_cast<std::uint8_t>(n.r0 & 0x0FU);
        n.mode = (n.r2 & 0x80U) != 0U;
        n.period_index = static_cast<std::uint8_t>(n.r2 & 0x0FU);
        n.length_load = static_cast<std::uint8_t>((n.r3 >> 3U) & 0x1FU);
    }

    void ricoh_2a03_apu::dmc_decode(dmc_channel& d) noexcept {
        d.irq_enable = (d.r0 & 0x80U) != 0U;
        d.loop_sample = (d.r0 & 0x40U) != 0U;
        d.rate_index = static_cast<std::uint8_t>(d.r0 & 0x0FU);
        d.direct_load = static_cast<std::uint8_t>(d.r1 & 0x7FU);
        d.sample_address =
            static_cast<std::uint16_t>(0xC000U + (static_cast<std::uint16_t>(d.r2) << 6U));
        d.sample_length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(d.r3) << 4U) + 1U);
    }

    void ricoh_2a03_apu::status_write(std::uint8_t value) noexcept {
        const bool e1 = (value & status_pulse1) != 0U;
        const bool e2 = (value & status_pulse2) != 0U;
        const bool et = (value & status_triangle) != 0U;
        const bool en = (value & status_noise) != 0U;
        const bool ed = (value & status_dmc) != 0U;
        pulse_[0].enabled = e1;
        pulse_[1].enabled = e2;
        triangle_.enabled = et;
        noise_.enabled = en;
        dmc_.enabled = ed;
        if (!e1) {
            pulse_[0].length_counter = 0U;
        }
        if (!e2) {
            pulse_[1].length_counter = 0U;
        }
        if (!et) {
            triangle_.length_counter = 0U;
        }
        if (!en) {
            noise_.length_counter = 0U;
        }
        // Writing $4015 clears DMC IRQ but not the frame IRQ.
        dmc_irq_flag_ = false;
    }

    void ricoh_2a03_apu::frame_counter_write(std::uint8_t value) noexcept {
        frame_mode_5step_ = (value & frame_mode_bit) != 0U;
        frame_irq_inhibit_ = (value & frame_irq_inhibit_bit) != 0U;
        // IRQ inhibit clears any pending frame IRQ.
        if (frame_irq_inhibit_) {
            frame_irq_flag_ = false;
        }
        // The 3-4 CPU-cycle restart delay is modelled as immediate (per the
        // reference): reset the cycle accumulator.
        cpu_cycles_ = 0U;
    }

    // ---- bus access ----

    std::uint8_t ricoh_2a03_apu::read_reg(std::uint16_t addr) noexcept {
        if (addr == reg_status) {
            std::uint8_t v = 0U;
            if (pulse_[0].length_counter > 0U) {
                v |= status_pulse1;
            }
            if (pulse_[1].length_counter > 0U) {
                v |= status_pulse2;
            }
            if (triangle_.length_counter > 0U) {
                v |= status_triangle;
            }
            if (noise_.length_counter > 0U) {
                v |= status_noise;
            }
            if (dmc_.enabled) {
                v |= status_dmc;
            }
            if (frame_irq_flag_) {
                v |= status_frame_irq;
            }
            if (dmc_irq_flag_) {
                v |= status_dmc_irq;
            }
            // Reading the status port clears the frame IRQ (but not the DMC IRQ).
            frame_irq_flag_ = false;
            return v;
        }
        // Every other APU address reads back as the open-bus latch.
        return open_bus_latch_;
    }

    void ricoh_2a03_apu::write_reg(std::uint16_t addr, std::uint8_t value) noexcept {
        open_bus_latch_ = value;
        switch (addr) {
        case reg_pulse1_0:
            pulse_[0].r0 = value;
            pulse_decode(pulse_[0]);
            break;
        case reg_pulse1_1:
            pulse_[0].r1 = value;
            pulse_decode(pulse_[0]);
            break;
        case reg_pulse1_2:
            pulse_[0].r2 = value;
            pulse_decode(pulse_[0]);
            break;
        case reg_pulse1_3:
            pulse_[0].r3 = value;
            pulse_decode(pulse_[0]);
            if (pulse_[0].enabled) {
                pulse_[0].length_counter = k_length_table[pulse_[0].length_load & 0x1FU];
            }
            pulse_[0].sequence_step = 0U; // a timer-high write restarts the duty phase
            break;

        case reg_pulse2_0:
            pulse_[1].r0 = value;
            pulse_decode(pulse_[1]);
            break;
        case reg_pulse2_1:
            pulse_[1].r1 = value;
            pulse_decode(pulse_[1]);
            break;
        case reg_pulse2_2:
            pulse_[1].r2 = value;
            pulse_decode(pulse_[1]);
            break;
        case reg_pulse2_3:
            pulse_[1].r3 = value;
            pulse_decode(pulse_[1]);
            if (pulse_[1].enabled) {
                pulse_[1].length_counter = k_length_table[pulse_[1].length_load & 0x1FU];
            }
            pulse_[1].sequence_step = 0U;
            break;

        case reg_tri_0:
            triangle_.r0 = value;
            triangle_decode(triangle_);
            break;
        case reg_tri_2:
            triangle_.r2 = value;
            triangle_decode(triangle_);
            break;
        case reg_tri_3:
            triangle_.r3 = value;
            triangle_decode(triangle_);
            if (triangle_.enabled) {
                triangle_.length_counter = k_length_table[triangle_.length_load & 0x1FU];
            }
            triangle_.linear_counter = triangle_.linear_reload;
            break;

        case reg_noise_0:
            noise_.r0 = value;
            noise_decode(noise_);
            break;
        case reg_noise_2:
            noise_.r2 = value;
            noise_decode(noise_);
            break;
        case reg_noise_3:
            noise_.r3 = value;
            noise_decode(noise_);
            if (noise_.enabled) {
                noise_.length_counter = k_length_table[noise_.length_load & 0x1FU];
            }
            break;

        case reg_dmc_0:
            dmc_.r0 = value;
            dmc_decode(dmc_);
            if (!dmc_.irq_enable) {
                dmc_irq_flag_ = false;
            }
            break;
        case reg_dmc_1:
            dmc_.r1 = value;
            dmc_decode(dmc_);
            dmc_.output_level = dmc_.direct_load; // $4011 loads the DAC directly
            break;
        case reg_dmc_2:
            dmc_.r2 = value;
            dmc_decode(dmc_);
            break;
        case reg_dmc_3:
            dmc_.r3 = value;
            dmc_decode(dmc_);
            break;

        case reg_status:
            status_write(value);
            break;
        case reg_frame_counter:
            frame_counter_write(value);
            break;

        default:
            break; // $4014 and unused slots are handled elsewhere / ignored.
        }
    }

    // ---- synthesis (clean-room from the 2A03 datasheet) ----

    std::int16_t ricoh_2a03_apu::mix_step() noexcept {
        std::int32_t mix = 0;

        for (auto& p : pulse_) {
            const bool gated = p.enabled && p.length_counter > 0U && p.timer >= 8U;
            // The pulse timer is clocked every other APU cycle; period+1 native
            // steps elapse before each duty-sequence advance.
            if (p.timer_counter == 0U) {
                p.timer_counter = p.timer;
                p.sequence_step = static_cast<std::uint8_t>((p.sequence_step + 1U) & 0x07U);
            } else {
                --p.timer_counter;
            }
            if (gated && k_duty_table[p.duty][p.sequence_step] != 0U) {
                // The envelope generator is not separately clocked in this
                // synthesis core, so volume_env is the level in both the
                // constant-volume and envelope-decay modes.
                mix += (k_channel_peak * static_cast<std::int32_t>(p.volume_env)) / 15;
            }
        }

        // Triangle: its sequencer advances whenever the channel is sounding (both
        // counters non-zero); the output is the 32-step ramp scaled to peak.
        {
            triangle_channel& t = triangle_;
            const bool gated =
                t.enabled && t.length_counter > 0U && t.linear_counter > 0U && t.timer >= 2U;
            if (t.timer_counter == 0U) {
                t.timer_counter = t.timer;
                if (gated) {
                    t.sequence_step = static_cast<std::uint8_t>((t.sequence_step + 1U) & 0x1FU);
                }
            } else {
                --t.timer_counter;
            }
            if (gated) {
                const std::int32_t lvl = k_triangle_table[t.sequence_step]; // 0..15
                mix += ((lvl - 7) * k_channel_peak) / 8;
            }
        }

        // Noise: a 15-bit LFSR clocked at the period-table rate; bit 0 (inverted)
        // gates the channel volume.
        {
            noise_channel& n = noise_;
            const bool gated = n.enabled && n.length_counter > 0U;
            const std::uint16_t period = k_noise_period[n.period_index & 0x0FU];
            if (n.timer_counter == 0U) {
                n.timer_counter = period;
                const std::uint16_t tap_bit = n.mode ? 6U : 1U;
                const std::uint16_t fb =
                    static_cast<std::uint16_t>((n.lfsr & 1U) ^ ((n.lfsr >> tap_bit) & 1U));
                n.lfsr = static_cast<std::uint16_t>((n.lfsr >> 1U) | (fb << 14U));
            } else {
                --n.timer_counter;
            }
            if (gated && (n.lfsr & 1U) == 0U) {
                mix += (k_channel_peak * static_cast<std::int32_t>(n.volume_env)) / 15;
            }
        }

        // DMC: no PCM stream is fetched here (that needs the CPU bus); the 7-bit DAC
        // level set via $4011 is mixed straight through so direct-load tones sound.
        if (dmc_.enabled) {
            mix += ((static_cast<std::int32_t>(dmc_.output_level) - 64) * k_channel_peak) / 64;
        }

        last_sample_ = static_cast<std::int16_t>(clamp16(mix));
        return last_sample_;
    }

    void ricoh_2a03_apu::step() noexcept { (void)mix_step(); }

    void ricoh_2a03_apu::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            const std::int16_t s = mix_step();
            buf_lr[i * 2U] = s;
            buf_lr[i * 2U + 1U] = s;
        }
    }

    void ricoh_2a03_apu::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            // Frame-counter IRQ edge gate (ported integer-exact from the reference).
            cpu_cycles_ += 1U;
            if (!frame_mode_5step_ && !frame_irq_inhibit_ &&
                cpu_cycles_ >= k_frame_4step_last_cyc) {
                frame_irq_flag_ = true;
            }
            if (frame_mode_5step_) {
                if (cpu_cycles_ >= k_frame_5step_last_cyc) {
                    cpu_cycles_ = 0U;
                }
            } else {
                if (cpu_cycles_ >= k_frame_4step_last_cyc) {
                    cpu_cycles_ = 0U;
                }
            }

            if (++prescaler_ >= clock_divider_) {
                prescaler_ = 0;
                const std::int16_t s = mix_step();
                if (audio_capture_) {
                    sample_queue_.push_back(s);
                    sample_queue_.push_back(s);
                }
            }
        }
    }

    std::size_t ricoh_2a03_apu::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
        // The queue holds interleaved (L,R) int16; counts are in stereo pairs.
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

    void ricoh_2a03_apu::reset(reset_kind /*kind*/) {
        // APU reset per the datasheet: $4015 cleared (no channels enabled); DMC
        // silenced; triangle linear counter cleared; length counters zeroed; frame
        // IRQ cleared. $4017 mode is preserved by leaving frame_mode_/inhibit_
        // untouched is NOT modelled by the reference (it memsets the whole struct),
        // so this mirrors the reference's full clear for bit-identical baseline.
        pulse_ = {};
        triangle_ = {};
        noise_ = {};
        noise_.lfsr = 1U; // LFSR powers up loaded (all-zero would never shift)
        dmc_ = {};
        frame_mode_5step_ = false;
        frame_irq_inhibit_ = false;
        frame_irq_flag_ = false;
        dmc_irq_flag_ = false;
        open_bus_latch_ = 0U;
        cpu_cycles_ = 0U;
        last_sample_ = 0;
        prescaler_ = 0;
        sample_queue_.clear();
    }

    void ricoh_2a03_apu::save_state(state_writer& writer) const {
        for (const auto& p : pulse_) {
            writer.u8(p.r0);
            writer.u8(p.r1);
            writer.u8(p.r2);
            writer.u8(p.r3);
            writer.u8(p.duty);
            writer.boolean(p.length_halt);
            writer.boolean(p.constant_volume);
            writer.u8(p.volume_env);
            writer.boolean(p.sweep_enable);
            writer.u8(p.sweep_period);
            writer.boolean(p.sweep_negate);
            writer.u8(p.sweep_shift);
            writer.u16(p.timer);
            writer.u8(p.length_load);
            writer.boolean(p.enabled);
            writer.u8(p.length_counter);
            writer.u16(p.timer_counter);
            writer.u8(p.sequence_step);
        }
        writer.u8(triangle_.r0);
        writer.u8(triangle_.r2);
        writer.u8(triangle_.r3);
        writer.boolean(triangle_.length_halt);
        writer.u8(triangle_.linear_reload);
        writer.u16(triangle_.timer);
        writer.u8(triangle_.length_load);
        writer.boolean(triangle_.enabled);
        writer.u8(triangle_.length_counter);
        writer.u16(triangle_.timer_counter);
        writer.u8(triangle_.sequence_step);
        writer.u8(triangle_.linear_counter);

        writer.u8(noise_.r0);
        writer.u8(noise_.r2);
        writer.u8(noise_.r3);
        writer.boolean(noise_.length_halt);
        writer.boolean(noise_.constant_volume);
        writer.u8(noise_.volume_env);
        writer.boolean(noise_.mode);
        writer.u8(noise_.period_index);
        writer.u8(noise_.length_load);
        writer.boolean(noise_.enabled);
        writer.u8(noise_.length_counter);
        writer.u16(noise_.timer_counter);
        writer.u16(noise_.lfsr);

        writer.u8(dmc_.r0);
        writer.u8(dmc_.r1);
        writer.u8(dmc_.r2);
        writer.u8(dmc_.r3);
        writer.boolean(dmc_.irq_enable);
        writer.boolean(dmc_.loop_sample);
        writer.u8(dmc_.rate_index);
        writer.u8(dmc_.direct_load);
        writer.u16(dmc_.sample_address);
        writer.u16(dmc_.sample_length);
        writer.boolean(dmc_.enabled);
        writer.u8(dmc_.output_level);

        writer.boolean(frame_mode_5step_);
        writer.boolean(frame_irq_inhibit_);
        writer.boolean(frame_irq_flag_);
        writer.boolean(dmc_irq_flag_);
        writer.u8(open_bus_latch_);
        writer.u64(cpu_cycles_);

        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_sample_));
    }

    void ricoh_2a03_apu::load_state(state_reader& reader) {
        for (auto& p : pulse_) {
            p.r0 = reader.u8();
            p.r1 = reader.u8();
            p.r2 = reader.u8();
            p.r3 = reader.u8();
            p.duty = reader.u8();
            p.length_halt = reader.boolean();
            p.constant_volume = reader.boolean();
            p.volume_env = reader.u8();
            p.sweep_enable = reader.boolean();
            p.sweep_period = reader.u8();
            p.sweep_negate = reader.boolean();
            p.sweep_shift = reader.u8();
            p.timer = reader.u16();
            p.length_load = reader.u8();
            p.enabled = reader.boolean();
            p.length_counter = reader.u8();
            p.timer_counter = reader.u16();
            p.sequence_step = reader.u8();
        }
        triangle_.r0 = reader.u8();
        triangle_.r2 = reader.u8();
        triangle_.r3 = reader.u8();
        triangle_.length_halt = reader.boolean();
        triangle_.linear_reload = reader.u8();
        triangle_.timer = reader.u16();
        triangle_.length_load = reader.u8();
        triangle_.enabled = reader.boolean();
        triangle_.length_counter = reader.u8();
        triangle_.timer_counter = reader.u16();
        triangle_.sequence_step = reader.u8();
        triangle_.linear_counter = reader.u8();

        noise_.r0 = reader.u8();
        noise_.r2 = reader.u8();
        noise_.r3 = reader.u8();
        noise_.length_halt = reader.boolean();
        noise_.constant_volume = reader.boolean();
        noise_.volume_env = reader.u8();
        noise_.mode = reader.boolean();
        noise_.period_index = reader.u8();
        noise_.length_load = reader.u8();
        noise_.enabled = reader.boolean();
        noise_.length_counter = reader.u8();
        noise_.timer_counter = reader.u16();
        noise_.lfsr = reader.u16();

        dmc_.r0 = reader.u8();
        dmc_.r1 = reader.u8();
        dmc_.r2 = reader.u8();
        dmc_.r3 = reader.u8();
        dmc_.irq_enable = reader.boolean();
        dmc_.loop_sample = reader.boolean();
        dmc_.rate_index = reader.u8();
        dmc_.direct_load = reader.u8();
        dmc_.sample_address = reader.u16();
        dmc_.sample_length = reader.u16();
        dmc_.enabled = reader.boolean();
        dmc_.output_level = reader.u8();

        frame_mode_5step_ = reader.boolean();
        frame_irq_inhibit_ = reader.boolean();
        frame_irq_flag_ = reader.boolean();
        dmc_irq_flag_ = reader.boolean();
        open_bus_latch_ = reader.u8();
        cpu_cycles_ = reader.u64();

        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_sample_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& ricoh_2a03_apu::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> ricoh_2a03_apu::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"P1_TIMER", pulse_[0].timer, 11U, fmt::unsigned_integer};
        register_view_[1] = {"P1_VOL", pulse_[0].volume_env, 4U, fmt::unsigned_integer};
        register_view_[2] = {"P2_TIMER", pulse_[1].timer, 11U, fmt::unsigned_integer};
        register_view_[3] = {"P2_VOL", pulse_[1].volume_env, 4U, fmt::unsigned_integer};
        register_view_[4] = {"TRI_TIMER", triangle_.timer, 11U, fmt::unsigned_integer};
        register_view_[5] = {"TRI_LEN", triangle_.length_counter, 8U, fmt::unsigned_integer};
        register_view_[6] = {"NOISE_PER", noise_.period_index, 4U, fmt::unsigned_integer};
        register_view_[7] = {"NOISE_LFSR", noise_.lfsr, 15U, fmt::unsigned_integer};
        register_view_[8] = {"DMC_DAC", dmc_.output_level, 7U, fmt::unsigned_integer};
        register_view_[9] = {"FRAME", static_cast<std::uint64_t>(frame_mode_5step_ ? 1U : 0U), 1U,
                             fmt::flags};
        register_view_[10] = {"FRAME_IRQ", static_cast<std::uint64_t>(frame_irq_flag_ ? 1U : 0U),
                              1U, fmt::flags};
        register_view_[11] = {"DMC_IRQ", static_cast<std::uint64_t>(dmc_irq_flag_ ? 1U : 0U), 1U,
                              fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ricoh_2a03_apu_registration = register_factory(
            "ricoh.2a03_apu", chip_class::audio_synth,
            []() -> std::unique_ptr<ichip> { return std::make_unique<ricoh_2a03_apu>(); });
    } // namespace

} // namespace mnemos::chips::audio
