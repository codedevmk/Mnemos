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

        // 4-bit rate index -> DMC output-clock period in CPU cycles (NTSC). Lower
        // index = slower playback; index 15 (54) is the ~33 kHz maximum.
        constexpr std::array<std::uint16_t, 16> k_dmc_rate = {
            428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

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

        // Frame-counter step CPU cycles, ported integer-exact (NTSC) + the PAL
        // 2A07 equivalents (the sequencer runs the same steps over a longer period).
        constexpr std::uint64_t k_frame_4step_last_cyc = 29829U;
        constexpr std::uint64_t k_frame_5step_last_cyc = 37281U;
        constexpr std::uint64_t k_frame_4step_last_cyc_pal = 33252U;
        constexpr std::uint64_t k_frame_5step_last_cyc_pal = 41565U;

        // PAL DMC output-clock periods in CPU cycles (the 2A07 rate table).
        constexpr std::array<std::uint16_t, 16> k_dmc_rate_pal = {
            398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50};

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
        // DMC enable ($4015 bit 4): setting it (re)starts the sample only when the
        // current one has finished; clearing it stops the memory reader.
        if (ed) {
            if (dmc_.bytes_remaining == 0U) {
                dmc_.current_address = dmc_.sample_address;
                dmc_.bytes_remaining = dmc_.sample_length;
            }
        } else {
            dmc_.bytes_remaining = 0U;
        }
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
        // Switching to 5-step mode immediately clocks a quarter + half frame.
        if (frame_mode_5step_) {
            clock_quarter_frame();
            clock_half_frame();
        }
    }

    void ricoh_2a03_apu::clock_quarter_frame() noexcept {
        // Volume envelopes (the two pulses + noise). The length-halt bit doubles as
        // the envelope loop flag.
        const auto clock_env = [](auto& ch) {
            if (ch.env_start) {
                ch.env_start = false;
                ch.env_decay = 15U;
                ch.env_divider = ch.volume_env;
            } else if (ch.env_divider == 0U) {
                ch.env_divider = ch.volume_env;
                if (ch.env_decay > 0U) {
                    --ch.env_decay;
                } else if (ch.length_halt) {
                    ch.env_decay = 15U; // loop
                }
            } else {
                --ch.env_divider;
            }
        };
        clock_env(pulse_[0]);
        clock_env(pulse_[1]);
        clock_env(noise_);

        // Triangle linear counter: reload while the flag is set, else count down;
        // the control bit (length_halt) clears the flag.
        if (triangle_.linear_reload_flag) {
            triangle_.linear_counter = triangle_.linear_reload;
        } else if (triangle_.linear_counter > 0U) {
            --triangle_.linear_counter;
        }
        if (!triangle_.length_halt) {
            triangle_.linear_reload_flag = false;
        }
    }

    void ricoh_2a03_apu::clock_half_frame() noexcept {
        // Length counters: a running, non-halted counter ticks down so the channel
        // actually stops. (Pulse sweep units are a later increment.)
        const auto clock_len = [](auto& ch) {
            if (!ch.length_halt && ch.length_counter > 0U) {
                --ch.length_counter;
            }
        };
        clock_len(pulse_[0]);
        clock_len(pulse_[1]);
        clock_len(triangle_);
        clock_len(noise_);
    }

    void ricoh_2a03_apu::notify_irq() noexcept {
        const bool now = irq_asserted();
        if (now != irq_last_) {
            irq_last_ = now;
            if (irq_cb_) {
                irq_cb_(now);
            }
        }
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
            notify_irq();
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
            pulse_[0].env_start = true;   // and restarts the volume envelope
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
            pulse_[1].env_start = true;
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
            triangle_.linear_reload_flag = true; // reloads on the next quarter-frame
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
            noise_.env_start = true;
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
        notify_irq(); // $4015/$4017/$4010 can acknowledge a pending IRQ
    }

    // ---- synthesis (clean-room from the 2A03 datasheet) ----

    void ricoh_2a03_apu::dmc_clock() noexcept {
        // Memory reader: keep the one-byte sample buffer full while bytes remain.
        // (The real chip steals ~4 CPU cycles per fetch for the DMA; that stall is
        // not modelled -- only the sample stream is, which is what makes it sound.)
        if (!dmc_.sample_buffer_full && dmc_.bytes_remaining > 0U) {
            dmc_.sample_buffer = dmc_read_ ? dmc_read_(dmc_.current_address) : 0U;
            dmc_.sample_buffer_full = true;
            dmc_.current_address = dmc_.current_address == 0xFFFFU
                                       ? 0x8000U
                                       : static_cast<std::uint16_t>(dmc_.current_address + 1U);
            --dmc_.bytes_remaining;
            if (dmc_.bytes_remaining == 0U) {
                if (dmc_.loop_sample) { // restart from the latched address/length
                    dmc_.current_address = dmc_.sample_address;
                    dmc_.bytes_remaining = dmc_.sample_length;
                } else if (dmc_.irq_enable) {
                    dmc_irq_flag_ = true;
                }
            }
        }

        // Output unit: the rate timer counts CPU cycles; on expiry it clocks one bit.
        if (dmc_.timer_counter != 0U) {
            --dmc_.timer_counter;
            return;
        }
        dmc_.timer_counter = (pal_ ? k_dmc_rate_pal : k_dmc_rate)[dmc_.rate_index & 0x0FU];
        if (!dmc_.silence) {
            // Delta step: +-2 per shifted bit, clamped to the 7-bit DAC range.
            if ((dmc_.shift_register & 0x01U) != 0U) {
                if (dmc_.output_level <= 125U) {
                    dmc_.output_level = static_cast<std::uint8_t>(dmc_.output_level + 2U);
                }
            } else if (dmc_.output_level >= 2U) {
                dmc_.output_level = static_cast<std::uint8_t>(dmc_.output_level - 2U);
            }
        }
        dmc_.shift_register = static_cast<std::uint8_t>(dmc_.shift_register >> 1U);
        if (dmc_.bits_remaining > 0U) {
            --dmc_.bits_remaining;
        }
        if (dmc_.bits_remaining == 0U) {
            // Start a new output cycle from the sample buffer, or fall silent.
            dmc_.bits_remaining = 8U;
            if (dmc_.sample_buffer_full) {
                dmc_.silence = false;
                dmc_.shift_register = dmc_.sample_buffer;
                dmc_.sample_buffer_full = false;
            } else {
                dmc_.silence = true;
            }
        }
    }

    void ricoh_2a03_apu::advance_oscillators(int cpu_cycles) noexcept {
        for (int c = 0; c < cpu_cycles; ++c) {
            // Triangle timer runs at the full CPU rate; its sequencer advances only
            // while the channel is sounding (both gating counters non-zero).
            {
                triangle_channel& t = triangle_;
                const bool on =
                    t.enabled && t.length_counter > 0U && t.linear_counter > 0U && t.timer >= 2U;
                if (t.timer_counter == 0U) {
                    t.timer_counter = t.timer;
                    if (on) {
                        t.sequence_step = static_cast<std::uint8_t>((t.sequence_step + 1U) & 0x1FU);
                    }
                } else {
                    --t.timer_counter;
                }
            }

            // Pulse + noise timers run at the APU rate -- every other CPU cycle.
            apu_half_ = !apu_half_;
            if (!apu_half_) {
                continue;
            }
            for (auto& p : pulse_) {
                if (p.timer_counter == 0U) {
                    p.timer_counter = p.timer;
                    p.sequence_step = static_cast<std::uint8_t>((p.sequence_step + 1U) & 0x07U);
                } else {
                    --p.timer_counter;
                }
            }
            {
                noise_channel& n = noise_;
                if (n.timer_counter == 0U) {
                    n.timer_counter = k_noise_period[n.period_index & 0x0FU];
                    const std::uint16_t tap_bit = n.mode ? 6U : 1U;
                    const std::uint16_t fb =
                        static_cast<std::uint16_t>((n.lfsr & 1U) ^ ((n.lfsr >> tap_bit) & 1U));
                    n.lfsr = static_cast<std::uint16_t>((n.lfsr >> 1U) | (fb << 14U));
                } else {
                    --n.timer_counter;
                }
            }
        }
    }

    std::int16_t ricoh_2a03_apu::mix_step() noexcept {
        // Run the tone oscillators across the CPU cycles this output sample spans,
        // then sample each channel's level and sum. Clocking the timers at the CPU
        // rate (not once per sample) is what makes the pitch correct.
        advance_oscillators(clock_divider_ > 0 ? clock_divider_ : 1);

        std::int32_t mix = 0;
        for (auto& p : pulse_) {
            const bool gated = p.enabled && p.length_counter > 0U && p.timer >= 8U;
            if (gated && k_duty_table[p.duty][p.sequence_step] != 0U) {
                // Constant-volume channels use the 4-bit volume; envelope channels
                // use the decay level clocked at the quarter-frame rate.
                const std::int32_t vol = p.constant_volume ? p.volume_env : p.env_decay;
                mix += (k_channel_peak * vol) / 15;
            }
        }
        {
            triangle_channel& t = triangle_;
            const bool gated =
                t.enabled && t.length_counter > 0U && t.linear_counter > 0U && t.timer >= 2U;
            if (gated) {
                const std::int32_t lvl = k_triangle_table[t.sequence_step]; // 0..15
                mix += ((lvl - 7) * k_channel_peak) / 8;
            }
        }
        {
            noise_channel& n = noise_;
            const bool gated = n.enabled && n.length_counter > 0U;
            if (gated && (n.lfsr & 1U) == 0U) {
                const std::int32_t vol = n.constant_volume ? n.volume_env : n.env_decay;
                mix += (k_channel_peak * vol) / 15;
            }
        }

        // DMC: the 7-bit DAC level -- driven by the delta-PCM output unit in
        // dmc_clock() (or loaded directly via $4011) -- mixed centred on mid-scale.
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
            dmc_clock(); // CPU-rate delta-PCM channel (memory reader + output unit)

            // Frame sequencer: every step clocks a quarter-frame (envelopes +
            // triangle linear counter); steps 1 and the last also clock a half-frame
            // (length counters). 4-step mode raises the frame IRQ on its last step.
            // Step cycles are NTSC/PAL (2A07) per pal_.
            const std::uint64_t s0 = pal_ ? 8313U : 7457U;
            const std::uint64_t s1 = pal_ ? 16627U : 14913U;
            const std::uint64_t s2 = pal_ ? 24939U : 22371U;
            const std::uint64_t s3 = pal_ ? k_frame_4step_last_cyc_pal : k_frame_4step_last_cyc;
            const std::uint64_t s4 = pal_ ? k_frame_5step_last_cyc_pal : k_frame_5step_last_cyc;
            cpu_cycles_ += 1U;
            if (frame_mode_5step_) {
                if (cpu_cycles_ == s0 || cpu_cycles_ == s2) {
                    clock_quarter_frame();
                } else if (cpu_cycles_ == s1) {
                    clock_quarter_frame();
                    clock_half_frame();
                } else if (cpu_cycles_ == s4) {
                    clock_quarter_frame();
                    clock_half_frame();
                    cpu_cycles_ = 0U;
                }
            } else {
                if (cpu_cycles_ == s0 || cpu_cycles_ == s2) {
                    clock_quarter_frame();
                } else if (cpu_cycles_ == s1) {
                    clock_quarter_frame();
                    clock_half_frame();
                } else if (cpu_cycles_ == s3) {
                    clock_quarter_frame();
                    clock_half_frame();
                    if (!frame_irq_inhibit_) {
                        frame_irq_flag_ = true;
                    }
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
        notify_irq(); // deliver a frame-/DMC-IRQ edge raised during this batch
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
        irq_last_ = false;
        apu_half_ = false;
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
            writer.boolean(p.env_start);
            writer.u8(p.env_divider);
            writer.u8(p.env_decay);
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
        writer.boolean(triangle_.linear_reload_flag);

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
        writer.boolean(noise_.env_start);
        writer.u8(noise_.env_divider);
        writer.u8(noise_.env_decay);

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
        writer.u16(dmc_.timer_counter);
        writer.u16(dmc_.current_address);
        writer.u16(dmc_.bytes_remaining);
        writer.u8(dmc_.shift_register);
        writer.u8(dmc_.bits_remaining);
        writer.u8(dmc_.sample_buffer);
        writer.boolean(dmc_.sample_buffer_full);
        writer.boolean(dmc_.silence);

        writer.boolean(frame_mode_5step_);
        writer.boolean(frame_irq_inhibit_);
        writer.boolean(frame_irq_flag_);
        writer.boolean(dmc_irq_flag_);
        writer.boolean(apu_half_);
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
            p.env_start = reader.boolean();
            p.env_divider = reader.u8();
            p.env_decay = reader.u8();
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
        triangle_.linear_reload_flag = reader.boolean();

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
        noise_.env_start = reader.boolean();
        noise_.env_divider = reader.u8();
        noise_.env_decay = reader.u8();

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
        dmc_.timer_counter = reader.u16();
        dmc_.current_address = reader.u16();
        dmc_.bytes_remaining = reader.u16();
        dmc_.shift_register = reader.u8();
        dmc_.bits_remaining = reader.u8();
        dmc_.sample_buffer = reader.u8();
        dmc_.sample_buffer_full = reader.boolean();
        dmc_.silence = reader.boolean();

        frame_mode_5step_ = reader.boolean();
        frame_irq_inhibit_ = reader.boolean();
        frame_irq_flag_ = reader.boolean();
        dmc_irq_flag_ = reader.boolean();
        apu_half_ = reader.boolean();
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
