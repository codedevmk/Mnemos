#include "qsound.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <utility>

namespace mnemos::chips::audio {
    namespace {
        constexpr std::uint32_t qsound_state_magic = 0x32445351U; // "QSD2", little-endian
        constexpr std::uint32_t qsound_state_version = 5U;
        constexpr std::uint16_t state_init1 = 0x0288U;
        constexpr std::uint16_t state_init2 = 0x061AU;
        constexpr std::uint16_t state_refresh1 = 0x0039U;
        constexpr std::uint16_t state_refresh2 = 0x004FU;
        constexpr std::uint16_t state_normal1 = 0x0314U;
        constexpr std::uint16_t state_normal2 = 0x06B2U;
        constexpr std::array<std::int16_t, 16> adpcm_step_scale = {
            154, 154, 128, 102, 77, 58, 58, 58,
            58,  58,  58,  58, 77, 102, 128, 154,
        };
        constexpr std::array<std::int16_t, 33> dry_pan_table = {
            -16384, -16384, -16384, -16384, -16384, -16384, -16384, -16384,
            -16384, -16384, -16384, -16384, -16384, -16384, -16384, -16384,
            -16384, -14746, -13107, -11633, -10486, -9175,  -8520,  -7209,
            -6226,  -5226,  -4588,  -3768,  -3277,  -2703,  -2130,  -1802,
            0,
        };
        constexpr std::array<std::int16_t, 33> wet_pan_table = {
            0,      -1638,  -1966,  -2458,  -2949,  -3441,  -4096,
            -4669,  -4915,  -5120,  -5489,  -6144,  -7537,  -8831,
            -9339,  -9830,  -10240, -10322, -10486, -10568, -10650,
            -11796, -12288, -12288, -12534, -12648, -12780, -12829,
            -12943, -13107, -13418, -14090, -16384,
        };
        constexpr std::array<std::int16_t, 33> linear_pan_table = {
            -16379, -16338, -16257, -16135, -15973, -15772, -15531,
            -15251, -14934, -14580, -14189, -13763, -13303, -12810,
            -12284, -11729, -11729, -11144, -10531, -9893,  -9229,
            -8543,  -7836,  -7109,  -6364,  -5604,  -4829,  -4043,
            -3246,  -2442,  -1631,  -817,   0,
        };
        constexpr std::array<std::array<std::int16_t, 95>, 5> filter_data = {{
            {{
                0,    0,    0,    6,    44,   -24,  -53,  -10,  59,   -40,
                -27,  1,    39,   -27,  56,   127,  174,  36,   -13,  49,
                212,  142,  143,  -73,  -20,  66,   -108, -117, -399, -265,
                -392, -569, -473, -71,  95,   -319, -218, -230, 331,  638,
                449,  477,  -180, 532,  1107, 750,  9899, 3828, -2418, 1071,
                -176, 191,  -431, 64,   117,  -150, -274, -97,  -238, 165,
                166,  250,  -19,  4,    37,   204,  186,  -6,   140,  -77,
                -1,   1,    18,   -10,  -151, -149, -103, -9,   55,   23,
                -102, -97,  -11,  13,   -48,  -27,  5,    18,   -61,  -30,
                64,   72,   0,    0,    0,
            }},
            {{
                0,    0,    0,    85,   24,   -76,  -123, -86,  -29,  -14,
                -20,  -7,   6,    -28,  -87,  -89,  -5,   100,  154,  160,
                150,  118,  41,   -48,  -78,  -23,  59,   83,   -2,   -176,
                -333, -344, -203, -66,  -39,  2,    224,  495,  495,  280,
                432,  1340, 2483, 5377, 1905, 658,  0,    97,   347,  285,
                35,   -95,  -78,  -82,  -151, -192, -171, -149, -147, -113,
                -22,  71,   118,  129,  127,  110,  71,   31,   20,   36,
                46,   23,   -27,  -63,  -53,  -21,  -19,  -60,  -92,  -69,
                -12,  25,   29,   30,   40,   41,   29,   30,   46,   39,
                -15,  -74,  0,    0,    0,
            }},
            {{
                0,    0,    0,    23,   42,   47,   29,   10,   2,    -14,
                -54,  -92,  -93,  -70,  -64,  -77,  -57,  18,   94,   113,
                87,   69,   67,   50,   25,   29,   58,   62,   24,   -39,
                -131, -256, -325, -234, -45,  58,   78,   223,  485,  496,
                127,  6,    857,  2283, 2683, 4928, 1328, 132,  79,   314,
                189,  -80,  -90,  35,   -21,  -186, -195, -99,  -136, -258,
                -189, 82,   257,  185,  53,   41,   84,   68,   38,   63,
                77,   14,   -60,  -71,  -71,  -120, -151, -84,  14,   29,
                -8,   7,    66,   69,   12,   -3,   54,   92,   52,   -6,
                -15,  -2,   0,    0,    0,
            }},
            {{
                0,    0,    0,    2,    -28,  -37,  -17,  0,    -9,   -22,
                -3,   35,   52,   39,   20,   7,    -6,   2,    55,   121,
                129,  67,   8,    1,    9,    -6,   -16,  16,   66,   96,
                118,  130,  75,   -47,  -92,  43,   223,  239,  151,  219,
                440,  475,  226,  206,  940,  2100, 2663, 4980, 865,  49,
                -33,  186,  231,  103,  42,   114,  191,  184,  116,  29,
                -47,  -72,  -21,  60,   96,   68,   31,   32,   63,   87,
                76,   39,   7,    14,   55,   85,   67,   18,   -12,  -3,
                21,   34,   29,   6,    -27,  -49,  -37,  -2,   16,   0,
                -21,  -16,  0,    0,    0,
            }},
            {{
                0,    0,    0,    48,   7,    -22,  -29,  -10,  24,   54,
                59,   29,   -36,  -117, -185, -213, -185, -99,  13,   90,
                83,   24,   -5,   23,   53,   47,   38,   56,   67,   57,
                75,   107,  16,   -242, -440, -355, -120, -33,  -47,  152,
                501,  472,  -57,  -292, 544,  1937, 2277, 6145, 1240, 153,
                47,   200,  152,  36,   64,   134,  74,   -82,  -208, -266,
                -268, -188, -42,  65,   74,   56,   89,   133,  114,  44,
                -3,   -1,   17,   29,   29,   -2,   -76,  -156, -187, -151,
                -85,  -31,  -5,   7,    20,   32,   24,   -5,   -20,  6,
                48,   62,   0,    0,    0,
            }},
        }};
        constexpr std::array<std::int16_t, 209> filter_data2 = {
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    -371, -196, -268, -512, -303,
            -315, -184, -76,  276,  -256, 298,  196,  990,  236,  1114,
            -126, 4377, 6549, 791,  0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    -16384, 0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
            0,    0,    0,    0,    0,    0,    0,    0,    0,
        };

        [[nodiscard]] std::int16_t clamp_i16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        }

        [[nodiscard]] std::int32_t clamp_i32(std::int32_t v, std::int32_t min_value,
                                             std::int32_t max_value) noexcept {
            if (v > max_value) {
                return max_value;
            }
            if (v < min_value) {
                return min_value;
            }
            return v;
        }

        [[nodiscard]] int sign_extend_adpcm_nibble(std::uint8_t nibble) noexcept {
            const int value = static_cast<int>(nibble & 0x0FU);
            return value >= 8 ? value - 16 : value;
        }

        [[nodiscard]] std::uint16_t bits16(std::int16_t value) noexcept {
            return static_cast<std::uint16_t>(value);
        }

        [[nodiscard]] std::uint16_t bits16(std::uint16_t value) noexcept {
            return value;
        }

        [[nodiscard]] std::uint16_t normalized_pan_index(std::uint16_t pan_reg) noexcept {
            std::uint16_t pan_addr = pan_reg;
            if (pan_addr < 0x0100U) {
                pan_addr = static_cast<std::uint16_t>(pan_addr + 0x0100U);
            }
            std::int32_t pan = static_cast<std::int32_t>(pan_addr) - 0x0110;
            if (pan < 0) {
                pan = 0;
            } else if (pan > 97) {
                pan = 97;
            }
            return static_cast<std::uint16_t>(pan);
        }

        [[nodiscard]] std::int32_t pan_gain(std::uint8_t channel, bool wet,
                                            std::uint16_t pan_reg) noexcept {
            const std::uint16_t pan = normalized_pan_index(pan_reg);
            if (wet) {
                if (pan <= 0x20U) {
                    return channel == 0U ? wet_pan_table[pan]
                                         : wet_pan_table[0x20U - pan];
                }
                return 0;
            }

            if (pan <= 0x20U) {
                return channel == 0U ? dry_pan_table[pan] : dry_pan_table[0x20U - pan];
            }
            if (pan >= 0x30U && pan <= 0x50U) {
                const std::uint16_t linear = static_cast<std::uint16_t>(pan - 0x30U);
                return channel == 0U ? linear_pan_table[linear]
                                     : linear_pan_table[0x20U - linear];
            }
            return 0;
        }

        [[nodiscard]] bool simple_mix_enabled() noexcept {
            static const bool enabled = [] {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in local audio diagnostic.
#endif
                const char* value = std::getenv("MNEMOS_QSOUND_SIMPLE_MIX");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
                return value != nullptr && value[0] != '\0' &&
                       !(value[0] == '0' && value[1] == '\0');
            }();
            return enabled;
        }

        constexpr int direct_mix_shift = 2;

        [[nodiscard]] std::pair<std::int32_t, std::int32_t>
        simple_pan_gains(std::uint16_t pan_reg) noexcept {
            std::int32_t pan = static_cast<std::int32_t>(pan_reg & 0x003FU) - 0x10;
            if (pan < 0) {
                pan = 0;
            } else if (pan > 0x20) {
                pan = 0x20;
            }
            return {0x20 - pan, pan};
        }
    } // namespace

    qsound::qsound() {
        initialize_trace_register_names();
        introspection_.with_registers([this] { return register_snapshot(); });
        reset(reset_kind::power_on);
    }

    void qsound::initialize_trace_register_names() noexcept {
        constexpr std::array<const char*, pcm_register_fields> voice_fields = {
            "BANK", "ADDR", "RATE", "PHASE", "LOOP",
            "END",  "VOL",  "PAN",  "ECHO",  "OUT"};
        for (std::size_t voice = 0U; voice < voice_count; ++voice) {
            for (std::size_t field = 0U; field < pcm_register_fields; ++field) {
                auto& name = voice_register_names_[voice * pcm_register_fields + field];
                std::snprintf(name.data(), name.size(), "PCM%02zu_%s", voice,
                              voice_fields[field]);
            }
        }

        constexpr std::array<const char*, register_histogram_fields> hist_fields = {
            "WR", "DATA", "PC"};
        for (std::size_t reg = 0U; reg < 256U; ++reg) {
            for (std::size_t field = 0U; field < register_histogram_fields; ++field) {
                auto& name =
                    histogram_register_names_[reg * register_histogram_fields + field];
                std::snprintf(name.data(), name.size(), "REG%02zX_%s", reg,
                              hist_fields[field]);
            }
        }

        constexpr std::array<const char*, register_trace_fields> fields = {
            "SEQ", "REG", "DATA", "PC"};
        for (std::size_t i = 0U; i < register_trace_capacity; ++i) {
            for (std::size_t field = 0U; field < register_trace_fields; ++field) {
                auto& name = trace_register_names_[i * register_trace_fields + field];
                std::snprintf(name.data(), name.size(), "TRACE%03zu_%s", i, fields[field]);
            }
        }
    }

    chip_metadata qsound::metadata() const noexcept {
        return {
            .manufacturer = "Capcom",
            .part_number = "DL-1425",
            .family = "QSound",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint8_t qsound::read_sample_u8(std::uint32_t rom_addr) const noexcept {
        if (rom_.empty() || rom_addr >= rom_.size()) {
            return 0U;
        }
        return rom_[rom_addr];
    }

    std::int16_t qsound::read_sample(std::uint16_t bank, std::uint16_t addr) const noexcept {
        const std::uint32_t rom_addr = ((static_cast<std::uint32_t>(bank) & 0x7FFFU) << 16U) |
                                       static_cast<std::uint32_t>(addr);
        const std::uint8_t sample = read_sample_u8(rom_addr);
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(sample) << 8U);
    }

    std::uint8_t qsound::read_adpcm_nibble(const adpcm_voice& voice,
                                           std::uint32_t nibble) const noexcept {
        const std::uint32_t rom_addr =
            ((static_cast<std::uint32_t>(voice.bank) & 0x7FFFU) << 16U) |
            static_cast<std::uint32_t>(voice.cur_addr);
        const std::uint8_t value = read_sample_u8(rom_addr);
        return (nibble & 1U) == 0U ? static_cast<std::uint8_t>((value >> 4U) & 0x0FU)
                                   : static_cast<std::uint8_t>(value & 0x0FU);
    }

    void qsound::step_adpcm(std::uint32_t voice_index, std::uint32_t nibble_phase) noexcept {
        adpcm_voice& voice = adpcm_[voice_index];
        std::int16_t& output = voice_output_[voice_count + voice_index];
        if (voice.flag == 0U && voice.play_volume == 0U) {
            voice.last_sample = 0;
            output = 0;
            return;
        }

        if ((nibble_phase & 1U) == 0U) {
            if (voice.cur_addr == voice.end_addr) {
                voice.play_volume = 0U;
                voice.cur_vol = 0;
                voice.last_sample = 0;
                output = 0;
            }

            if (voice.flag != 0U && voice.volume != 0U) {
                voice.flag = 0U;
                voice.play_volume = voice.volume;
                voice.cur_addr = voice.start_addr;
                voice.step_size = 10;
                voice.cur_vol = 0;
                voice.last_sample = 0;
                output = 0;
            }
        }

        if (voice.play_volume == 0U) {
            voice.last_sample = 0;
            output = 0;
            return;
        }

        // ADPCM step size is clamped to its valid range after both the initial load and
        // the post-decode rescale; both sites use this identical clamp.
        const auto clamp_step_size = [](std::int16_t step_value) noexcept -> std::int16_t {
            return static_cast<std::int16_t>(
                clamp_i32(step_value, adpcm_min_step_size, adpcm_max_step_size));
        };
        voice.step_size = clamp_step_size(voice.step_size);

        const int step = sign_extend_adpcm_nibble(read_adpcm_nibble(voice, nibble_phase));
        std::int32_t delta =
            (1 + (step < 0 ? -step : step) * 2) * static_cast<std::int32_t>(voice.step_size);
        delta >>= 1;
        if (step <= 0) {
            delta = -delta;
        }

        const std::int32_t predictor =
            clamp_i32(static_cast<std::int32_t>(voice.cur_vol) + delta, -32768, 32767);
        voice.cur_vol = static_cast<std::int16_t>(predictor);

        voice.step_size = static_cast<std::int16_t>(
            (static_cast<std::int32_t>(adpcm_step_scale[static_cast<std::size_t>(8 + step)]) *
             static_cast<std::int32_t>(voice.step_size)) >>
            6);
        voice.step_size = clamp_step_size(voice.step_size);

        if ((nibble_phase & 1U) != 0U) {
            ++voice.cur_addr;
        }

        const std::int32_t sample =
            (predictor * static_cast<std::int32_t>(voice.play_volume)) >> 16;
        voice.last_sample = clamp_i16(sample);
        output = voice.last_sample;
    }

    void qsound::write_register(std::uint8_t reg, std::uint16_t data,
                                std::uint16_t writer_pc) noexcept {
        if (reg < 0x80U) {
            const std::uint32_t voice_index = static_cast<std::uint32_t>(reg) >> 3U;
            const std::uint8_t voice_reg = static_cast<std::uint8_t>(reg & 0x07U);
            voice& v = voices_[voice_index];
            switch (voice_reg) {
            case 0:
                // The bank register programs the NEXT voice's bank (hardware quirk).
                voices_[(voice_index + 1U) & 0x0FU].bank = data;
                break;
            case 1:
                v.addr = data;
                break;
            case 2:
                v.rate = data;
                break;
            case 3:
                v.phase = data;
                break;
            case 4:
                v.loop_len = data;
                break;
            case 5:
                v.end_addr = data;
                break;
            case 6:
                v.volume = data;
                break;
            default:
                break;
            }
            return;
        }
        if (reg >= 0x80U && reg < 0x80U + voice_count) {
            voices_[reg - 0x80U].pan = data;
            return;
        }
        if (reg >= 0x90U && reg < 0x90U + adpcm_voice_count) {
            adpcm_[reg - 0x90U].pan = data;
            return;
        }
        if (reg == 0x93U) {
            echo_feedback_ = static_cast<std::int16_t>(data);
            return;
        }
        if (reg >= 0xCAU && reg < 0xCAU + adpcm_voice_count * 4U) {
            const std::uint32_t voice_index = static_cast<std::uint32_t>((reg - 0xCAU) >> 2U);
            const std::uint8_t voice_reg = static_cast<std::uint8_t>((reg - 0xCAU) & 0x03U);
            adpcm_voice& v = adpcm_[voice_index];
            switch (voice_reg) {
            case 0:
                v.start_addr = data;
                break;
            case 1:
                v.end_addr = data;
                break;
            case 2:
                v.bank = data;
                break;
            case 3:
                v.volume = data;
                if (data != 0U) {
                    ++nonzero_adpcm_volume_write_count_;
                    last_nonzero_adpcm_volume_voice_ = static_cast<std::uint8_t>(voice_index);
                    last_nonzero_adpcm_volume_data_ = data;
                    last_nonzero_adpcm_volume_pc_ = writer_pc;
                }
                break;
            default:
                break;
            }
            return;
        }
        if (reg >= 0xD6U && reg < 0xD6U + adpcm_voice_count) {
            const std::uint32_t voice_index = static_cast<std::uint32_t>(reg - 0xD6U);
            adpcm_[voice_index].flag = data;
            if (data != 0U) {
                ++adpcm_trigger_count_;
            }
            last_adpcm_trigger_voice_ = static_cast<std::uint8_t>(voice_index);
            last_adpcm_trigger_flag_ = data;
            last_adpcm_trigger_pc_ = writer_pc;
            return;
        }
        if (reg == 0xD9U) {
            echo_end_pos_ = data;
            if (echo_delay_pos_ >= echo_delay_length()) {
                echo_delay_pos_ = 0U;
            }
            return;
        }
        if (reg >= 0xBAU && reg < 0xBAU + voice_count) {
            voices_[reg - 0xBAU].echo = static_cast<std::int16_t>(data);
            return;
        }

        switch (reg) {
        case 0xDAU:
            filter_[0].table_pos = data;
            break;
        case 0xDBU:
            alt_filter_[0].table_pos = data;
            break;
        case 0xDCU:
            filter_[1].table_pos = data;
            break;
        case 0xDDU:
            alt_filter_[1].table_pos = data;
            break;
        case 0xDEU:
            wet_[0].delay = static_cast<std::int16_t>(data);
            break;
        case 0xDFU:
            dry_[0].delay = static_cast<std::int16_t>(data);
            break;
        case 0xE0U:
            wet_[1].delay = static_cast<std::int16_t>(data);
            break;
        case 0xE1U:
            dry_[1].delay = static_cast<std::int16_t>(data);
            break;
        case 0xE2U:
            delay_update_ = data;
            break;
        case 0xE3U:
            next_state_ = data;
            break;
        case 0xE4U:
            wet_[0].volume = static_cast<std::int16_t>(data);
            break;
        case 0xE5U:
            dry_[0].volume = static_cast<std::int16_t>(data);
            break;
        case 0xE6U:
            wet_[1].volume = static_cast<std::int16_t>(data);
            break;
        case 0xE7U:
            dry_[1].volume = static_cast<std::int16_t>(data);
            break;
        default:
            break;
        }
    }

    void qsound::write_port(std::uint8_t offset, std::uint8_t value) noexcept {
        write_port_with_pc(offset, value, 0U);
    }

    void qsound::write_port_with_pc(std::uint8_t offset, std::uint8_t value,
                                    std::uint16_t writer_pc) noexcept {
        ++port_write_count_;
        switch (offset & 0x03U) {
        case 0:
            data_latch_ = static_cast<std::uint16_t>((data_latch_ & 0x00FFU) |
                                                     (static_cast<std::uint16_t>(value) << 8U));
            break;
        case 1:
            data_latch_ = static_cast<std::uint16_t>((data_latch_ & 0xFF00U) | value);
            break;
        case 2: {
            ++register_write_count_;
            ++register_write_histogram_[value];
            register_last_data_[value] = data_latch_;
            register_last_pc_[value] = writer_pc;
            last_register_ = value;
            last_register_data_ = data_latch_;
            last_register_pc_ = writer_pc;
            const std::uint32_t trace_index =
                register_trace_count_ % static_cast<std::uint32_t>(register_trace_.size());
            register_trace_[trace_index] = register_trace_entry{.sequence = register_trace_count_,
                                                                .reg = value,
                                                                .data = data_latch_,
                                                                .pc = writer_pc};
            ++register_trace_count_;
            if (value < 0x80U && (value & 0x07U) == 6U && data_latch_ != 0U) {
                ++nonzero_pcm_volume_write_count_;
                last_nonzero_pcm_volume_reg_ = value;
                last_nonzero_pcm_volume_data_ = data_latch_;
                last_nonzero_pcm_volume_pc_ = writer_pc;
            }
            write_register(value, data_latch_, writer_pc);
            if (ready_mode_ == ready_mode::immediate) {
                ready_ = ready_flag;
                ready_cycle_accum_ = 0U;
            } else {
                ready_ = 0U;
                ready_cycle_accum_ = 0U;
            }
            break;
        }
        default:
            break;
        }
    }

    void qsound::advance_command_ready(std::uint64_t cycles) noexcept {
        if (ready_mode_ == ready_mode::immediate) {
            ready_ = ready_flag;
            ready_cycle_accum_ = 0U;
            return;
        }
        if (ready_ == ready_flag) {
            return;
        }
        const std::uint64_t accumulated =
            static_cast<std::uint64_t>(ready_cycle_accum_) + cycles;
        if (accumulated >= command_ready_cycles) {
            ready_ = ready_flag;
            ready_cycle_accum_ = 0U;
        } else {
            ready_cycle_accum_ = static_cast<std::uint32_t>(accumulated);
        }
    }

    void qsound::initialize_mode(std::uint8_t mode) noexcept {
        for (voice& v : voices_) {
            v = voice{};
        }
        for (adpcm_voice& v : adpcm_) {
            v = adpcm_voice{};
        }
        voice_output_.fill(0);
        filter_ = {};
        alt_filter_ = {};
        wet_ = {};
        dry_ = {};
        reset_echo_state();

        if (mode == 0U) {
            wet_[0].delay = 0;
            dry_[0].delay = 46;
            wet_[1].delay = 0;
            dry_[1].delay = 48;
            filter_[0].table_pos = 0x0DB2U;
            filter_[1].table_pos = 0x0E11U;
            echo_end_pos_ = echo_delay_base + 6U;
            next_state_ = state_refresh1;
        } else {
            wet_[0].delay = 1;
            dry_[0].delay = 0;
            wet_[1].delay = 0;
            dry_[1].delay = 0;
            filter_[0].table_pos = 0x0F73U;
            filter_[1].table_pos = 0x0FA4U;
            alt_filter_[0].table_pos = 0x0F73U;
            alt_filter_[1].table_pos = 0x0FA4U;
            echo_end_pos_ = mode2_echo_delay_base + 6U;
            next_state_ = state_refresh2;
        }

        wet_[0].volume = 0x3FFF;
        dry_[0].volume = 0x3FFF;
        wet_[1].volume = 0x3FFF;
        dry_[1].volume = 0x3FFF;
        delay_update_ = 1U;
        ready_ = 0U;
        ready_cycle_accum_ = 0U;
        state_counter_ = 1U;
    }

    void qsound::initialize_mode1_defaults() noexcept {
        initialize_mode(0U);
        refresh_filter_mode1();
        state_ = state_normal1;
        next_state_ = state_normal1;
        state_counter_ = 0U;
        ready_ = ready_flag;
        ready_cycle_accum_ = 0U;
    }

    void qsound::load_filter(fir_filter& f, std::int16_t tap_count) noexcept {
        f.delay_pos = 0;
        f.tap_count = tap_count;
        f.taps.fill(0);
        std::size_t count = 0U;
        if (const std::int16_t* table = filter_table(f.table_pos, count)) {
            const std::size_t copy_count =
                std::min<std::size_t>(count, static_cast<std::size_t>(tap_count));
            std::copy_n(table, copy_count, f.taps.begin());
        }
    }

    void qsound::refresh_filter_mode1() noexcept {
        for (std::size_t ch = 0; ch < filter_.size(); ++ch) {
            load_filter(filter_[ch], static_cast<std::int16_t>(filter_tap_capacity));
        }
        state_ = state_normal1;
        next_state_ = state_normal1;
    }

    void qsound::refresh_filter_mode2() noexcept {
        for (std::size_t ch = 0; ch < filter_.size(); ++ch) {
            load_filter(filter_[ch], 45);
            load_filter(alt_filter_[ch], 44);
        }
        state_ = state_normal2;
        next_state_ = state_normal2;
    }

    const std::int16_t* qsound::filter_table(std::uint16_t offset,
                                             std::size_t& count) const noexcept {
        count = 0U;
        if (offset >= 0x0F2EU && offset < 0x0FFFU) {
            const std::size_t index = static_cast<std::size_t>(offset - 0x0F2EU);
            count = filter_data2.size() - std::min(index, filter_data2.size());
            return index < filter_data2.size() ? &filter_data2[index] : nullptr;
        }
        if (offset < 0x0D53U) {
            return nullptr;
        }
        const std::uint16_t table = static_cast<std::uint16_t>((offset - 0x0D53U) / 95U);
        if (table >= filter_data.size()) {
            return nullptr;
        }
        count = filter_tap_capacity;
        return filter_data[table].data();
    }

    void qsound::update_sample() noexcept {
        switch (state_) {
        case state_init1:
        case state_init2:
            if (state_counter_ >= 2U) {
                state_counter_ = 0U;
                state_ = next_state_;
                return;
            }
            if (state_counter_ == 1U) {
                ++state_counter_;
                return;
            }
            initialize_mode(state_ == state_init2 ? 1U : 0U);
            return;
        case state_refresh1:
            refresh_filter_mode1();
            return;
        case state_refresh2:
            refresh_filter_mode2();
            return;
        case state_normal1:
        case state_normal2:
            update_normal_sample();
            return;
        default:
            initialize_mode(0U);
            return;
        }
    }

    std::int16_t qsound::pcm_update(voice& v, std::int32_t& echo_out) noexcept {
        const std::int16_t output = static_cast<std::int16_t>(
            (static_cast<std::int32_t>(v.volume) *
             static_cast<std::int32_t>(read_sample(v.bank, v.addr))) >>
            14);
        echo_out += (static_cast<std::int32_t>(output) * static_cast<std::int32_t>(v.echo)) << 2;

        std::int32_t phase =
            static_cast<std::int32_t>((static_cast<std::uint32_t>(v.addr) << 12U) |
                                      static_cast<std::uint32_t>(v.phase >> 4U));
        phase += static_cast<std::int32_t>(static_cast<std::uint32_t>(v.rate));
        if ((phase >> 12) >= static_cast<std::int32_t>(v.end_addr)) {
            phase -= static_cast<std::int32_t>(static_cast<std::uint32_t>(v.loop_len) << 12U);
        }
        // A voice address is a full 16-bit sample index; with the 12-bit phase
        // fraction the accumulator spans 28 bits, so the high bound must be
        // 0x0FFFFFFF. The previous 0x07FFFFFF cap pinned any voice whose address
        // reached 0x8000 to 0x7FFF, silencing every sample in the upper half of a
        // bank (the melodic PCM voices) while low-half voices kept playing.
        phase = clamp_i32(phase, -0x10000000, 0x0FFFFFFF);
        v.addr = static_cast<std::uint16_t>(phase >> 12);
        v.phase = static_cast<std::uint16_t>((static_cast<std::uint32_t>(phase) << 4U) & 0xFFFFU);
        return output;
    }

    std::int16_t qsound::echo(std::int32_t input) noexcept {
        if (echo_length_ == 0U) {
            echo_delay_pos_ = 0U;
            echo_last_sample_ = 0;
            return 0;
        }
        if (echo_delay_pos_ >= echo_length_) {
            echo_delay_pos_ = 0U;
        }

        const std::int32_t old_sample = echo_delay_[echo_delay_pos_];
        const std::int32_t last_sample = echo_last_sample_;
        echo_last_sample_ = static_cast<std::int16_t>(old_sample);
        const std::int32_t averaged = (old_sample + last_sample) >> 1;
        const std::int32_t new_sample =
            input + ((averaged * static_cast<std::int32_t>(echo_feedback_)) << 2);
        echo_delay_[echo_delay_pos_] = static_cast<std::int16_t>(new_sample >> 16);
        ++echo_delay_pos_;
        if (echo_delay_pos_ >= echo_length_) {
            echo_delay_pos_ = 0U;
        }
        return static_cast<std::int16_t>(averaged);
    }

    std::int32_t qsound::fir(fir_filter& filter, std::int16_t input) noexcept {
        const std::int16_t tap_count = filter.tap_count;
        if (tap_count <= 0) {
            return 0;
        }
        if (tap_count == 1) {
            return -(static_cast<std::int32_t>(filter.taps[0]) * input) << 2;
        }

        std::int32_t output = 0;
        std::int16_t tap = 0;
        for (; tap < tap_count - 1; ++tap) {
            output -= (static_cast<std::int32_t>(filter.taps[tap]) *
                       filter.delay_line[filter.delay_pos]) << 2;
            ++filter.delay_pos;
            if (filter.delay_pos >= tap_count - 1) {
                filter.delay_pos = 0;
            }
        }

        output -= (static_cast<std::int32_t>(filter.taps[tap]) * input) << 2;
        filter.delay_line[filter.delay_pos] = input;
        ++filter.delay_pos;
        if (filter.delay_pos >= tap_count - 1) {
            filter.delay_pos = 0;
        }
        return output;
    }

    std::int32_t qsound::delay(mix_delay& line, std::int32_t input) noexcept {
        line.delay_line[line.write_pos] = static_cast<std::int16_t>(input >> 16);
        ++line.write_pos;
        if (line.write_pos >= static_cast<std::int16_t>(delay_line_capacity)) {
            line.write_pos = 0;
        }

        const std::int32_t output =
            static_cast<std::int32_t>(line.delay_line[line.read_pos]) * line.volume;
        ++line.read_pos;
        if (line.read_pos >= static_cast<std::int16_t>(delay_line_capacity)) {
            line.read_pos = 0;
        }
        return output;
    }

    void qsound::delay_update(mix_delay& line) noexcept {
        std::int32_t read_pos =
            (static_cast<std::int32_t>(line.write_pos) - line.delay) %
            static_cast<std::int32_t>(delay_line_capacity);
        if (read_pos < 0) {
            read_pos += static_cast<std::int32_t>(delay_line_capacity);
        }
        line.read_pos = static_cast<std::int16_t>(read_pos);
    }

    void qsound::update_normal_sample() noexcept {
        ready_ = ready_flag;
        ready_cycle_accum_ = 0U;

        const std::int32_t echo_base =
            state_ == state_normal2 ? mode2_echo_delay_base : echo_delay_base;
        const std::int32_t echo_length =
            static_cast<std::int32_t>(echo_end_pos_) - echo_base;
        echo_length_ = static_cast<std::uint16_t>(clamp_i32(echo_length, 0, 1024));
        if (echo_delay_pos_ >= echo_length_) {
            echo_delay_pos_ = 0U;
        }

        const bool direct_mix =
            mixer_mode_ == mixer_mode::direct_pan || simple_mix_enabled();
        std::int32_t echo_input = 0;
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            if (direct_mix && voices_[i].volume == 0) {
                voice_output_[i] = 0;
                continue;
            }
            voice_output_[i] = pcm_update(voices_[i], echo_input);
        }

        const std::uint32_t adpcm_voice = state_counter_ % adpcm_voice_count;
        const std::uint32_t adpcm_nibble = state_counter_ / adpcm_voice_count;
        step_adpcm(adpcm_voice, adpcm_nibble);

        if (direct_mix) {
            std::int32_t left = 0;
            std::int32_t right = 0;
            for (std::size_t i = 0; i < voices_.size(); ++i) {
                const auto [left_gain, right_gain] = simple_pan_gains(voices_[i].pan);
                const std::int32_t sample = voice_output_[i];
                left += (sample * left_gain) >> 5;
                right += (sample * right_gain) >> 5;
            }
            for (std::size_t i = 0; i < adpcm_.size(); ++i) {
                left += voice_output_[voices_.size() + i];
                right += voice_output_[voices_.size() + i];
            }
            last_l_ = clamp_i16(left >> direct_mix_shift);
            last_r_ = clamp_i16(right >> direct_mix_shift);
            ++state_counter_;
            if (state_counter_ > 5U) {
                state_counter_ = 0U;
                state_ = next_state_;
            }
            return;
        }

        constexpr std::int32_t mix_bus_limit = 0x1FFFFFFF;
        constexpr std::int32_t echo_bus_scale = 1 << 14;
        constexpr std::int32_t pre_filter_scale = 4;
        const std::int16_t echo_output = echo(echo_input);
        for (std::uint8_t ch = 0; ch < 2U; ++ch) {
            std::int32_t wet =
                ch == 1U ? static_cast<std::int32_t>(echo_output) * echo_bus_scale : 0;
            std::int32_t dry =
                ch == 0U ? static_cast<std::int32_t>(echo_output) * echo_bus_scale : 0;

            for (std::size_t i = 0; i < voice_output_.size(); ++i) {
                const std::uint16_t pan =
                    i < voices_.size() ? voices_[i].pan : adpcm_[i - voices_.size()].pan;
                const std::int32_t sample = voice_output_[i];
                dry -= sample * pan_gain(ch, false, pan);
                wet -= sample * pan_gain(ch, true, pan);
            }

            dry = clamp_i32(dry, -mix_bus_limit, mix_bus_limit) * pre_filter_scale;
            wet = clamp_i32(wet, -mix_bus_limit, mix_bus_limit) * pre_filter_scale;

            wet = fir(filter_[ch], static_cast<std::int16_t>(wet >> 16));
            if (state_ == state_normal2) {
                dry = fir(alt_filter_[ch], static_cast<std::int16_t>(dry >> 16));
            }

            std::int32_t output = delay(wet_[ch], wet) + delay(dry_[ch], dry);
            output = (output + 0x2000) & ~0x3FFF;
            if (ch == 0U) {
                last_l_ = clamp_i16(output >> 14);
            } else {
                last_r_ = clamp_i16(output >> 14);
            }

            if (delay_update_ != 0U) {
                delay_update(wet_[ch]);
                delay_update(dry_[ch]);
            }
        }

        delay_update_ = 0U;
        ++state_counter_;
        if (state_counter_ > 5U) {
            state_counter_ = 0U;
            state_ = next_state_;
        }
    }

    void qsound::step() noexcept {
        update_sample();
    }

    void qsound::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_l_;
            buf_lr[i * 2U + 1U] = last_r_;
        }
    }

    void qsound::tick(std::uint64_t cycles) {
        // The HLE mixer is drained by generate()/step(), but the command handshake
        // is board-clocked unless the board selects the legacy immediate-ready path.
        advance_command_ready(cycles);
    }

    void qsound::set_ready_mode(ready_mode mode) noexcept {
        ready_mode_ = mode;
        if (ready_mode_ == ready_mode::immediate) {
            ready_ = ready_flag;
            ready_cycle_accum_ = 0U;
        }
    }

    void qsound::reset(reset_kind /*kind*/) {
        // The sample ROM is host-owned (never cleared here); every reset kind
        // clears the same voice + latch state back to power-on QSound defaults.
        initialize_mode1_defaults();
        data_latch_ = 0U;
        ready_ = ready_flag;
        ready_cycle_accum_ = 0U;
        port_write_count_ = 0U;
        register_write_count_ = 0U;
        register_write_histogram_.fill(0U);
        register_last_data_.fill(0U);
        register_last_pc_.fill(0U);
        nonzero_pcm_volume_write_count_ = 0U;
        last_nonzero_pcm_volume_reg_ = 0U;
        last_nonzero_pcm_volume_data_ = 0U;
        last_nonzero_pcm_volume_pc_ = 0U;
        nonzero_adpcm_volume_write_count_ = 0U;
        last_nonzero_adpcm_volume_voice_ = 0U;
        last_nonzero_adpcm_volume_data_ = 0U;
        last_nonzero_adpcm_volume_pc_ = 0U;
        adpcm_trigger_count_ = 0U;
        last_adpcm_trigger_voice_ = 0U;
        last_adpcm_trigger_flag_ = 0U;
        last_adpcm_trigger_pc_ = 0U;
        last_register_ = 0U;
        last_register_data_ = 0U;
        last_register_pc_ = 0U;
        register_trace_count_ = 0U;
        register_trace_.fill(register_trace_entry{});
        adpcm_phase_ = 0U;
        last_l_ = 0;
        last_r_ = 0;
    }

    void qsound::reset_echo_state() noexcept {
        echo_delay_.fill(0);
        echo_end_pos_ = echo_delay_base + 6U;
        echo_length_ = 6U;
        echo_feedback_ = 0;
        echo_delay_pos_ = 0U;
        echo_last_sample_ = 0;
    }

    std::uint16_t qsound::echo_delay_length() const noexcept {
        const std::uint16_t base =
            state_ == state_normal2 || next_state_ == state_normal2 ? mode2_echo_delay_base
                                                                    : echo_delay_base;
        if (echo_end_pos_ <= base) {
            return 0U;
        }
        const std::uint32_t length =
            static_cast<std::uint32_t>(echo_end_pos_) - base;
        return static_cast<std::uint16_t>(
            length > echo_delay_capacity ? echo_delay_capacity : length);
    }

    void qsound::save_state(state_writer& writer) const {
        writer.u32(qsound_state_magic);
        writer.u32(qsound_state_version);
        for (const voice& v : voices_) {
            writer.u16(v.bank);
            writer.u16(bits16(v.addr));
            writer.u16(v.rate);
            writer.u16(v.phase);
            writer.u16(bits16(v.loop_len));
            writer.u16(bits16(v.end_addr));
            writer.u16(bits16(v.volume));
            writer.u16(v.pan);
            writer.u16(bits16(v.echo));
        }
        for (const adpcm_voice& v : adpcm_) {
            writer.u16(v.start_addr);
            writer.u16(v.end_addr);
            writer.u16(v.bank);
            writer.u16(v.volume);
            writer.u16(v.pan);
            writer.u16(v.play_volume);
            writer.u16(v.flag);
            writer.u16(v.cur_addr);
            writer.u16(static_cast<std::uint16_t>(v.step_size));
            writer.u16(static_cast<std::uint16_t>(v.cur_vol));
            writer.u16(static_cast<std::uint16_t>(v.last_sample));
        }
        writer.u16(data_latch_);
        writer.u8(ready_);
        writer.u32(ready_cycle_accum_);
        writer.u32(adpcm_phase_);
        writer.u16(echo_end_pos_);
        writer.u16(static_cast<std::uint16_t>(echo_feedback_));
        writer.u16(echo_delay_pos_);
        writer.u16(static_cast<std::uint16_t>(echo_last_sample_));
        for (const std::int16_t sample : echo_delay_) {
            writer.u16(static_cast<std::uint16_t>(sample));
        }
        for (const std::int16_t sample : voice_output_) {
            writer.u16(static_cast<std::uint16_t>(sample));
        }
        for (const fir_filter& f : filter_) {
            writer.u16(f.table_pos);
            writer.u16(static_cast<std::uint16_t>(f.tap_count));
            writer.u16(static_cast<std::uint16_t>(f.delay_pos));
            for (const std::int16_t tap : f.taps) {
                writer.u16(static_cast<std::uint16_t>(tap));
            }
            for (const std::int16_t sample : f.delay_line) {
                writer.u16(static_cast<std::uint16_t>(sample));
            }
        }
        for (const fir_filter& f : alt_filter_) {
            writer.u16(f.table_pos);
            writer.u16(static_cast<std::uint16_t>(f.tap_count));
            writer.u16(static_cast<std::uint16_t>(f.delay_pos));
            for (const std::int16_t tap : f.taps) {
                writer.u16(static_cast<std::uint16_t>(tap));
            }
            for (const std::int16_t sample : f.delay_line) {
                writer.u16(static_cast<std::uint16_t>(sample));
            }
        }
        for (const mix_delay& d : wet_) {
            writer.u16(static_cast<std::uint16_t>(d.delay));
            writer.u16(static_cast<std::uint16_t>(d.volume));
            writer.u16(static_cast<std::uint16_t>(d.write_pos));
            writer.u16(static_cast<std::uint16_t>(d.read_pos));
            for (const std::int16_t sample : d.delay_line) {
                writer.u16(static_cast<std::uint16_t>(sample));
            }
        }
        for (const mix_delay& d : dry_) {
            writer.u16(static_cast<std::uint16_t>(d.delay));
            writer.u16(static_cast<std::uint16_t>(d.volume));
            writer.u16(static_cast<std::uint16_t>(d.write_pos));
            writer.u16(static_cast<std::uint16_t>(d.read_pos));
            for (const std::int16_t sample : d.delay_line) {
                writer.u16(static_cast<std::uint16_t>(sample));
            }
        }
        writer.u16(state_);
        writer.u16(next_state_);
        writer.u16(delay_update_);
        writer.u16(state_counter_);
        writer.u16(echo_length_);
        writer.u16(static_cast<std::uint16_t>(last_l_));
        writer.u16(static_cast<std::uint16_t>(last_r_));
        writer.u32(port_write_count_);
        writer.u32(register_write_count_);
        for (const std::uint32_t count : register_write_histogram_) {
            writer.u32(count);
        }
        for (const std::uint16_t data : register_last_data_) {
            writer.u16(data);
        }
        for (const std::uint16_t pc : register_last_pc_) {
            writer.u16(pc);
        }
        writer.u32(nonzero_pcm_volume_write_count_);
        writer.u8(last_nonzero_pcm_volume_reg_);
        writer.u16(last_nonzero_pcm_volume_data_);
        writer.u16(last_nonzero_pcm_volume_pc_);
        writer.u32(nonzero_adpcm_volume_write_count_);
        writer.u8(last_nonzero_adpcm_volume_voice_);
        writer.u16(last_nonzero_adpcm_volume_data_);
        writer.u16(last_nonzero_adpcm_volume_pc_);
        writer.u32(adpcm_trigger_count_);
        writer.u8(last_adpcm_trigger_voice_);
        writer.u16(last_adpcm_trigger_flag_);
        writer.u16(last_adpcm_trigger_pc_);
        writer.u8(last_register_);
        writer.u16(last_register_data_);
        writer.u16(last_register_pc_);
        writer.u32(register_trace_count_);
        for (const register_trace_entry& entry : register_trace_) {
            writer.u32(entry.sequence);
            writer.u8(entry.reg);
            writer.u16(entry.data);
            writer.u16(entry.pc);
        }
    }

    void qsound::load_state(state_reader& reader) {
        const std::uint32_t marker = reader.u32();
        const bool legacy = marker != qsound_state_magic;
        std::uint32_t version = 0U;
        if (!legacy) {
            version = reader.u32();
            if (version == 0U || version > qsound_state_version) {
                reader.fail();
                return;
            }
        }
        initialize_mode1_defaults();
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            voice& v = voices_[i];
            if (legacy && i == 0U) {
                v.bank = static_cast<std::uint16_t>(marker & 0xFFFFU);
                v.addr = static_cast<std::uint16_t>(marker >> 16U);
            } else {
                v.bank = reader.u16();
                v.addr = reader.u16();
            }
            v.rate = reader.u16();
            v.phase = reader.u16();
            v.loop_len = reader.u16();
            v.end_addr = reader.u16();
            v.volume = reader.u16();
            v.pan = reader.u16();
            v.echo = static_cast<std::int16_t>(reader.u16());
        }
        if (legacy) {
            for (adpcm_voice& v : adpcm_) {
                v = adpcm_voice{};
            }
        } else {
            for (adpcm_voice& v : adpcm_) {
                v.start_addr = reader.u16();
                v.end_addr = reader.u16();
                v.bank = reader.u16();
                v.volume = reader.u16();
                v.pan = version >= 5U ? reader.u16() : default_pan;
                v.play_volume = reader.u16();
                v.flag = reader.u16();
                v.cur_addr = reader.u16();
                v.step_size = static_cast<std::int16_t>(reader.u16());
                v.cur_vol = static_cast<std::int16_t>(reader.u16());
                v.last_sample = static_cast<std::int16_t>(reader.u16());
            }
        }
        data_latch_ = reader.u16();
        ready_ = reader.u8();
        ready_cycle_accum_ = (!legacy && version >= 4U) ? reader.u32() : 0U;
        adpcm_phase_ = legacy ? 0U : reader.u32();
        if (!legacy && version >= 2U) {
            echo_end_pos_ = reader.u16();
            echo_feedback_ = static_cast<std::int16_t>(reader.u16());
            echo_delay_pos_ = reader.u16();
            echo_last_sample_ = static_cast<std::int16_t>(reader.u16());
            for (std::int16_t& sample : echo_delay_) {
                sample = static_cast<std::int16_t>(reader.u16());
            }
            if (echo_delay_pos_ >= echo_delay_length()) {
                echo_delay_pos_ = 0U;
            }
        } else {
            reset_echo_state();
        }
        if (!legacy && version >= 5U) {
            for (std::int16_t& sample : voice_output_) {
                sample = static_cast<std::int16_t>(reader.u16());
            }
            // Clamp restored filter indices: tap_count/delay_pos drive fir() array
            // indexing, so a corrupt or truncated save must not push them out of range.
            const auto read_filter = [&reader](fir_filter& f) {
                f.table_pos = reader.u16();
                f.tap_count = static_cast<std::int16_t>(
                    clamp_i32(reader.u16(), 0, static_cast<std::int32_t>(filter_tap_capacity)));
                f.delay_pos = static_cast<std::int16_t>(clamp_i32(
                    reader.u16(), 0, static_cast<std::int32_t>(filter_tap_capacity) - 1));
                for (std::int16_t& tap : f.taps) {
                    tap = static_cast<std::int16_t>(reader.u16());
                }
                for (std::int16_t& sample : f.delay_line) {
                    sample = static_cast<std::int16_t>(reader.u16());
                }
            };
            for (fir_filter& f : filter_) {
                read_filter(f);
            }
            for (fir_filter& f : alt_filter_) {
                read_filter(f);
            }
            const auto read_delay = [&reader](mix_delay& d) {
                d.delay = static_cast<std::int16_t>(reader.u16());
                d.volume = static_cast<std::int16_t>(reader.u16());
                d.write_pos = static_cast<std::int16_t>(clamp_i32(
                    reader.u16(), 0, static_cast<std::int32_t>(delay_line_capacity) - 1));
                d.read_pos = static_cast<std::int16_t>(clamp_i32(
                    reader.u16(), 0, static_cast<std::int32_t>(delay_line_capacity) - 1));
                for (std::int16_t& sample : d.delay_line) {
                    sample = static_cast<std::int16_t>(reader.u16());
                }
            };
            for (mix_delay& d : wet_) {
                read_delay(d);
            }
            for (mix_delay& d : dry_) {
                read_delay(d);
            }
            state_ = reader.u16();
            next_state_ = reader.u16();
            delay_update_ = reader.u16();
            state_counter_ = reader.u16();
            echo_length_ = reader.u16();
        } else {
            // Pre-v5 saves carry no filter/delay/echo-state block. Echo length/state for
            // such legacy blobs is intentionally lossy (recomputed from current defaults);
            // the live writer emits v5, so this branch is backward-compatibility only.
            voice_output_.fill(0);
            echo_length_ = echo_delay_length();
        }
        last_l_ = static_cast<std::int16_t>(reader.u16());
        last_r_ = static_cast<std::int16_t>(reader.u16());
        if (!legacy && version >= 3U) {
            port_write_count_ = reader.u32();
            register_write_count_ = reader.u32();
            for (std::uint32_t& count : register_write_histogram_) {
                count = reader.u32();
            }
            for (std::uint16_t& data : register_last_data_) {
                data = reader.u16();
            }
            for (std::uint16_t& pc : register_last_pc_) {
                pc = reader.u16();
            }
            nonzero_pcm_volume_write_count_ = reader.u32();
            last_nonzero_pcm_volume_reg_ = reader.u8();
            last_nonzero_pcm_volume_data_ = reader.u16();
            last_nonzero_pcm_volume_pc_ = reader.u16();
            nonzero_adpcm_volume_write_count_ = reader.u32();
            last_nonzero_adpcm_volume_voice_ = reader.u8();
            last_nonzero_adpcm_volume_data_ = reader.u16();
            last_nonzero_adpcm_volume_pc_ = reader.u16();
            adpcm_trigger_count_ = reader.u32();
            last_adpcm_trigger_voice_ = reader.u8();
            last_adpcm_trigger_flag_ = reader.u16();
            last_adpcm_trigger_pc_ = reader.u16();
            last_register_ = reader.u8();
            last_register_data_ = reader.u16();
            last_register_pc_ = reader.u16();
            register_trace_count_ = reader.u32();
            for (register_trace_entry& entry : register_trace_) {
                entry.sequence = reader.u32();
                entry.reg = reader.u8();
                entry.data = reader.u16();
                entry.pc = reader.u16();
            }
        } else {
            port_write_count_ = 0U;
            register_write_count_ = 0U;
            register_write_histogram_.fill(0U);
            register_last_data_.fill(0U);
            register_last_pc_.fill(0U);
            nonzero_pcm_volume_write_count_ = 0U;
            last_nonzero_pcm_volume_reg_ = 0U;
            last_nonzero_pcm_volume_data_ = 0U;
            last_nonzero_pcm_volume_pc_ = 0U;
            nonzero_adpcm_volume_write_count_ = 0U;
            last_nonzero_adpcm_volume_voice_ = 0U;
            last_nonzero_adpcm_volume_data_ = 0U;
            last_nonzero_adpcm_volume_pc_ = 0U;
            adpcm_trigger_count_ = 0U;
            last_adpcm_trigger_voice_ = 0U;
            last_adpcm_trigger_flag_ = 0U;
            last_adpcm_trigger_pc_ = 0U;
            last_register_ = 0U;
            last_register_data_ = 0U;
            last_register_pc_ = 0U;
            register_trace_count_ = 0U;
            register_trace_.fill(register_trace_entry{});
        }
    }

    instrumentation::ichip_introspection& qsound::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> qsound::register_snapshot() noexcept {
        using fmt = register_value_format;
        constexpr std::array<std::array<const char*, adpcm_register_fields>,
                             adpcm_voice_count>
            adpcm_names = {{
                {"ADPCM0_START", "ADPCM0_END", "ADPCM0_BANK", "ADPCM0_VOL",
                 "ADPCM0_PLAY", "ADPCM0_FLAG", "ADPCM0_CUR", "ADPCM0_STEP",
                 "ADPCM0_PRED", "ADPCM0_LAST"},
                {"ADPCM1_START", "ADPCM1_END", "ADPCM1_BANK", "ADPCM1_VOL",
                 "ADPCM1_PLAY", "ADPCM1_FLAG", "ADPCM1_CUR", "ADPCM1_STEP",
                 "ADPCM1_PRED", "ADPCM1_LAST"},
                {"ADPCM2_START", "ADPCM2_END", "ADPCM2_BANK", "ADPCM2_VOL",
                 "ADPCM2_PLAY", "ADPCM2_FLAG", "ADPCM2_CUR", "ADPCM2_STEP",
                 "ADPCM2_PRED", "ADPCM2_LAST"},
            }};

        std::size_t out = 0U;
        const auto add = [this, &out](const char* name, std::uint64_t value,
                                      std::uint8_t bit_width, fmt format) {
            register_view_[out++] = {name, value, bit_width, format};
        };

        add("READY", ready_, 8U, fmt::flags);
        add("MIXMODE", mixer_mode_ == mixer_mode::direct_pan ? 1U : 0U, 8U,
            fmt::unsigned_integer);
        add("V0BANK", voices_[0].bank, 16U, fmt::unsigned_integer);
        add("V0ADDR", bits16(voices_[0].addr), 16U, fmt::unsigned_integer);
        add("V0VOL", bits16(voices_[0].volume), 16U, fmt::unsigned_integer);
        add("ECHOFB", static_cast<std::uint16_t>(echo_feedback_), 16U,
            fmt::signed_integer);
        add("ECHOLEN", echo_delay_length(), 16U, fmt::unsigned_integer);
        add("PORTWR", port_write_count_, 32U, fmt::unsigned_integer);
        add("REGWR", register_write_count_, 32U, fmt::unsigned_integer);
        add("TRACECOUNT", register_trace_count_, 32U, fmt::unsigned_integer);
        add("LASTREG", last_register_, 8U, fmt::unsigned_integer);
        add("LASTDATA", last_register_data_, 16U, fmt::unsigned_integer);
        add("LASTPC", last_register_pc_, 16U, fmt::unsigned_integer);
        add("PCM_VOLWR", nonzero_pcm_volume_write_count_, 32U, fmt::unsigned_integer);
        add("ADPCM_VOLWR", nonzero_adpcm_volume_write_count_, 32U, fmt::unsigned_integer);
        add("ADPCM_TRIG", adpcm_trigger_count_, 32U, fmt::unsigned_integer);

        for (std::size_t i = 0U; i < voices_.size(); ++i) {
            const voice& v = voices_[i];
            const std::size_t base = i * pcm_register_fields;
            add(voice_register_names_[base + 0U].data(), v.bank, 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 1U].data(), bits16(v.addr), 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 2U].data(), v.rate, 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 3U].data(), v.phase, 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 4U].data(), bits16(v.loop_len), 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 5U].data(), bits16(v.end_addr), 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 6U].data(), bits16(v.volume), 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 7U].data(), v.pan, 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 8U].data(), bits16(v.echo), 16U,
                fmt::unsigned_integer);
            add(voice_register_names_[base + 9U].data(),
                static_cast<std::uint16_t>(voice_output_[i]), 16U, fmt::signed_integer);
        }

        for (std::size_t i = 0U; i < adpcm_.size(); ++i) {
            const adpcm_voice& v = adpcm_[i];
            const auto& names = adpcm_names[i];
            add(names[0], v.start_addr, 16U, fmt::unsigned_integer);
            add(names[1], v.end_addr, 16U, fmt::unsigned_integer);
            add(names[2], v.bank, 16U, fmt::unsigned_integer);
            add(names[3], v.volume, 16U, fmt::unsigned_integer);
            add(names[4], v.play_volume, 16U, fmt::unsigned_integer);
            add(names[5], v.flag, 16U, fmt::flags);
            add(names[6], v.cur_addr, 16U, fmt::unsigned_integer);
            add(names[7], static_cast<std::uint16_t>(v.step_size), 16U,
                fmt::signed_integer);
            add(names[8], static_cast<std::uint16_t>(v.cur_vol), 16U,
                fmt::signed_integer);
            add(names[9], static_cast<std::uint16_t>(v.last_sample), 16U,
                fmt::signed_integer);
        }

        for (std::size_t reg = 0U; reg < register_write_histogram_.size(); ++reg) {
            const std::uint32_t writes = register_write_histogram_[reg];
            if (writes == 0U) {
                continue;
            }
            const std::size_t name_index = reg * register_histogram_fields;
            add(histogram_register_names_[name_index + 0U].data(), writes, 32U,
                fmt::unsigned_integer);
            add(histogram_register_names_[name_index + 1U].data(),
                register_last_data_[reg], 16U, fmt::unsigned_integer);
            add(histogram_register_names_[name_index + 2U].data(), register_last_pc_[reg],
                16U, fmt::unsigned_integer);
        }

        const std::uint32_t trace_total = register_trace_count_;
        const std::uint32_t trace_kept =
            trace_total > register_trace_capacity
                ? static_cast<std::uint32_t>(register_trace_capacity)
                : trace_total;
        const std::uint32_t trace_start = trace_total - trace_kept;
        for (std::uint32_t i = 0U; i < trace_kept; ++i) {
            const register_trace_entry entry = register_trace(trace_start + i);
            const std::size_t name_index = static_cast<std::size_t>(i) * register_trace_fields;
            add(trace_register_names_[name_index + 0U].data(), entry.sequence, 32U,
                fmt::unsigned_integer);
            add(trace_register_names_[name_index + 1U].data(), entry.reg, 8U,
                fmt::unsigned_integer);
            add(trace_register_names_[name_index + 2U].data(), entry.data, 16U,
                fmt::unsigned_integer);
            add(trace_register_names_[name_index + 3U].data(), entry.pc, 16U,
                fmt::unsigned_integer);
        }

        return std::span<const register_descriptor>{register_view_.data(), out};
    }

    namespace {
        [[maybe_unused]] const auto qsound_registration =
            register_factory("capcom.qsound", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<qsound>(); });
    } // namespace

} // namespace mnemos::chips::audio
