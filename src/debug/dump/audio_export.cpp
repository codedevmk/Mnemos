#include "audio_export.hpp"

#include "audio_views.hpp"
#include "file.hpp"
#include "introspection_views.hpp"
#include "json_util.hpp"
#include "path_id.hpp"
#include "wav.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::debug {

    namespace {
        struct runtime_reg_write final {
            std::uint16_t port{};
            std::uint8_t value{};
        };

        struct runtime_reg_write_target final {
            chips::ichip* chip{};
            std::string id{};
            std::string part_number{};
            std::vector<runtime_reg_write> events{};
        };

        [[nodiscard]] bool is_sound_comm_chip(const chips::ichip& chip) noexcept {
            return chip.metadata().klass == chips::chip_class::bus_controller &&
                   chip.metadata().part_number == "TC0140SYT";
        }

        [[nodiscard]] bool is_runtime_sound_cpu_chip(const chips::ichip& chip) noexcept {
            return chip.metadata().klass == chips::chip_class::cpu &&
                   chip.metadata().part_number == "Z80";
        }

        [[nodiscard]] bool is_runtime_sound_comm_register(std::string_view name) noexcept {
            return name == "MPORT" || name == "SPORT" || name.starts_with("M2S") ||
                   name.starts_with("S2M") || name == "STATUS" || name == "M2SPEND" ||
                   name == "S2MPEND" || name == "MRDPH" || name == "MWRPH" ||
                   name == "SRDPH" || name == "SWRPH" || name.starts_with("CMD") ||
                   name.starts_with("RPL") || name == "CLEAR" || name == "LCMDP" ||
                   name == "LCMD" || name == "LCMRP" || name == "LCMR" ||
                   name == "LRPWP" || name == "LRPW" || name == "LRPRP" ||
                   name == "LRPR";
        }

        [[nodiscard]] bool is_runtime_sound_cpu_register(std::string_view name) noexcept {
            return name == "PC" || name == "IFF1" || name == "IFF2" || name == "HALT" ||
                   name == "IRQ" || name == "NMIPEND" || name == "NMILINE" ||
                   name == "RESET" || name == "NMIACC" || name == "IRQACC";
        }

        [[nodiscard]] int abs_sample(std::int16_t sample) noexcept {
            const int value = static_cast<int>(sample);
            return value < 0 ? -value : value;
        }

        void append_runtime_audio_registers_json(std::string& json,
                                                 const frontend_sdk::player_system& sys) {
            json += ", \"chips\": [";
            bool first_chip = true;
            for (chips::ichip* chip : sys.chips()) {
                if (chip == nullptr) {
                    continue;
                }
                instrumentation::register_view* regs = chip->introspection().registers();
                if (regs == nullptr) {
                    continue;
                }
                const bool audio_synth = chip->metadata().klass == chips::chip_class::audio_synth;
                const bool sound_comm = is_sound_comm_chip(*chip);
                const bool sound_cpu = is_runtime_sound_cpu_chip(*chip);
                if (!audio_synth && !sound_comm && !sound_cpu) {
                    continue;
                }
                const std::span<const chips::register_descriptor> descriptors =
                    regs->registers();
                bool has_runtime_audio = false;
                for (const chips::register_descriptor& d : descriptors) {
                    has_runtime_audio =
                        has_runtime_audio || audio_synth ||
                        (sound_comm ? is_runtime_sound_comm_register(d.name)
                                    : is_runtime_sound_cpu_register(d.name));
                }
                if (!has_runtime_audio) {
                    continue;
                }

                json += first_chip ? "" : ", ";
                first_chip = false;
                json += "{\"id\": " + json_string(sanitize_id(chip->metadata().part_number)) +
                        ", \"part_number\": " + json_string(chip->metadata().part_number) +
                        ", \"registers\": [";
                bool first_register = true;
                for (const chips::register_descriptor& d : descriptors) {
                    if (!audio_synth &&
                        (sound_comm ? !is_runtime_sound_comm_register(d.name)
                                    : !is_runtime_sound_cpu_register(d.name))) {
                        continue;
                    }
                    json += first_register ? "" : ", ";
                    first_register = false;
                    json += "{\"name\": " + json_string(d.name) +
                            ", \"value\": " + std::to_string(d.value) +
                            ", \"bits\": " + std::to_string(d.bit_width) + "}";
                }
                json += "]}";
            }
            json += "]";
        }

        [[nodiscard]] std::vector<runtime_reg_write_target>
        install_runtime_audio_write_traces(frontend_sdk::player_system& sys) {
            std::vector<runtime_reg_write_target> targets;
            const auto chips = sys.chips();
            targets.reserve(chips.size());
            for (chips::ichip* chip : chips) {
                if (chip == nullptr ||
                    chip->metadata().klass != chips::chip_class::audio_synth ||
                    chip->introspection().reg_writes() == nullptr) {
                    continue;
                }
                targets.push_back(runtime_reg_write_target{
                    .chip = chip,
                    .id = sanitize_id(chip->metadata().part_number),
                    .part_number = std::string(chip->metadata().part_number),
                    .events = {}});
            }
            for (runtime_reg_write_target& target : targets) {
                instrumentation::reg_write_trace* trace =
                    target.chip->introspection().reg_writes();
                trace->install([events = &target.events](
                                   const instrumentation::reg_write_event& event) {
                    events->push_back(
                        runtime_reg_write{.port = event.port, .value = event.value});
                });
            }
            return targets;
        }

        void uninstall_runtime_audio_write_traces(
            std::vector<runtime_reg_write_target>& targets) noexcept {
            for (runtime_reg_write_target& target : targets) {
                if (target.chip == nullptr) {
                    continue;
                }
                if (auto* trace = target.chip->introspection().reg_writes()) {
                    trace->install({});
                }
            }
        }

        void append_runtime_audio_writes_json(
            std::string& json,
            const std::vector<runtime_reg_write_target>& targets) {
            json += ", \"register_writes\": [";
            bool first_chip = true;
            for (const runtime_reg_write_target& target : targets) {
                if (target.events.empty()) {
                    continue;
                }
                json += first_chip ? "" : ", ";
                first_chip = false;
                json += "{\"id\": " + json_string(target.id) +
                        ", \"part_number\": " + json_string(target.part_number) +
                        ", \"writes\": [";
                for (std::size_t i = 0; i < target.events.size(); ++i) {
                    const runtime_reg_write& event = target.events[i];
                    json += i == 0 ? "" : ", ";
                    json += "{\"port\": " + std::to_string(event.port) +
                            ", \"value\": " + std::to_string(event.value) + "}";
                }
                json += "]}";
            }
            json += "]";
        }
    } // namespace

    std::size_t export_audio(const frontend_sdk::player_system& sys, const std::string& base_path) {
        std::size_t written = 0;
        std::string json = "{\n  \"chips\": [";
        bool first_chip = true;

        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr) {
                continue;
            }
            instrumentation::audio_source* src = chip->introspection().audio();
            instrumentation::register_view* regs = chip->introspection().registers();
            const bool is_audio = chip->metadata().klass == chips::chip_class::audio_synth;
            // Include a chip when it exposes PCM samples, or it is an audio chip
            // exposing its register file. A non-audio chip's registers (e.g. the
            // CPU's) belong in a debug/CPU dump, not the audio manifest.
            if (src == nullptr && !(is_audio && regs != nullptr)) {
                continue;
            }
            const std::string chip_id = sanitize_id(chip->metadata().part_number);
            const std::span<const instrumentation::sample_view> samples =
                src != nullptr ? src->samples() : std::span<const instrumentation::sample_view>{};

            json += first_chip ? "\n" : ",\n";
            first_chip = false;
            json += "    {\n      \"id\": " + json_string(chip_id) +
                    ",\n      \"part_number\": " + json_string(chip->metadata().part_number) +
                    ",\n      \"samples\": [";

            for (std::size_t i = 0; i < samples.size(); ++i) {
                const instrumentation::sample_view& s = samples[i];
                const std::string path =
                    base_path + "." + chip_id + ".sample." + sanitize_id(s.name) + ".wav";
                if (audio::write_wav(path, s.frames, s.sample_rate, s.channels)) {
                    ++written;
                } else {
                    std::fprintf(stderr, "[audio_export] could not write %s\n", path.c_str());
                }
                json += i == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_string(s.name) +
                        ", \"sample_rate\": " + std::to_string(s.sample_rate) +
                        ", \"channels\": " + std::to_string(s.channels) +
                        ", \"frames\": " + std::to_string(s.frame_count()) +
                        ", \"loop_start\": " + std::to_string(s.loop_start) +
                        ", \"source_addr\": " + std::to_string(s.source_addr) +
                        ", \"file\": " + json_string(path_basename(path)) + "}";
            }
            json += samples.empty() ? "],\n      \"registers\": ["
                                    : "\n      ],\n      \"registers\": [";

            // Reuse the chip's register_view (register_snapshot) rather than a
            // bespoke "voice" type: a synth's voice/instrument state is its
            // register file. Chips that don't expose registers() emit an empty
            // array.
            const std::span<const chips::register_descriptor> descriptors =
                regs != nullptr ? regs->registers() : std::span<const chips::register_descriptor>{};
            for (std::size_t i = 0; i < descriptors.size(); ++i) {
                const chips::register_descriptor& d = descriptors[i];
                json += i == 0 ? "\n" : ",\n";
                json += "        {\"name\": " + json_string(d.name) +
                        ", \"value\": " + std::to_string(d.value) +
                        ", \"bits\": " + std::to_string(d.bit_width) + "}";
            }
            json += descriptors.empty() ? "]\n    }" : "\n      ]\n    }";
        }

        json += first_chip ? "]\n}\n" : "\n  ]\n}\n";

        const std::string manifest_path = base_path + ".audio.json";
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
        if (!io::write_file(manifest_path, bytes)) {
            std::fprintf(stderr, "[audio_export] could not write %s\n", manifest_path.c_str());
        }
        return written;
    }

    std::size_t export_rendered_audio(frontend_sdk::player_system& sys, std::uint64_t frames,
                                      const std::string& base_path,
                                      const std::function<void(std::uint64_t)>& before_frame) {
        std::vector<std::int16_t> pcm; // interleaved L,R across the whole capture
        std::uint32_t sample_rate = 0U;
        std::string frame_json;
        bool first_frame = true;
        std::vector<runtime_reg_write_target> write_targets =
            install_runtime_audio_write_traces(sys);
        for (std::uint64_t i = 0; i < frames; ++i) {
            for (runtime_reg_write_target& target : write_targets) {
                target.events.clear();
            }
            if (before_frame) {
                before_frame(i);
            }
            sys.step_one_frame();
            const frontend_sdk::audio_chunk chunk = sys.drain_audio();
            if (chunk.sample_rate != 0U) {
                sample_rate = chunk.sample_rate;
            }

            std::uint64_t sum_abs = 0U;
            int peak_abs = 0;
            std::size_t nonzero_frames = 0U;
            if (chunk.samples != nullptr && chunk.frame_count != 0U) {
                for (std::uint32_t frame = 0; frame < chunk.frame_count; ++frame) {
                    int frame_peak = 0;
                    const std::size_t offset = static_cast<std::size_t>(frame) * 2U;
                    for (std::size_t ch = 0; ch < 2U; ++ch) {
                        const int abs = abs_sample(chunk.samples[offset + ch]);
                        sum_abs += static_cast<std::uint64_t>(abs);
                        frame_peak = std::max(frame_peak, abs);
                        peak_abs = std::max(peak_abs, abs);
                    }
                    if (frame_peak != 0) {
                        ++nonzero_frames;
                    }
                }
                pcm.insert(pcm.end(), chunk.samples,
                           chunk.samples + static_cast<std::size_t>(chunk.frame_count) * 2U);
            }

            const double mean_abs =
                chunk.frame_count == 0U
                    ? 0.0
                    : static_cast<double>(sum_abs) /
                          static_cast<double>(static_cast<std::uint64_t>(chunk.frame_count) * 2U);
            frame_json += first_frame ? "\n    " : ",\n    ";
            first_frame = false;
            frame_json += "{\"frame\": " + std::to_string(i + 1U) +
                          ", \"audio_frames\": " + std::to_string(chunk.frame_count) +
                          ", \"sample_rate\": " + std::to_string(chunk.sample_rate) +
                          ", \"nonzero_frames\": " + std::to_string(nonzero_frames) +
                          ", \"peak_abs\": " + std::to_string(peak_abs) +
                          ", \"mean_abs\": " + std::to_string(mean_abs);
            append_runtime_audio_registers_json(frame_json, sys);
            append_runtime_audio_writes_json(frame_json, write_targets);
            frame_json += "}";
        }

        if (sample_rate == 0U) {
            sample_rate = 48000U; // nothing drained -> still emit a valid (silent) WAV
        }
        const std::string path = base_path + ".rendered.wav";
        if (!audio::write_wav(path, pcm, sample_rate, 2U)) {
            uninstall_runtime_audio_write_traces(write_targets);
            std::fprintf(stderr, "[audio_export] could not write %s\n", path.c_str());
            return 0U;
        }

        std::string trace = "{\n";
        trace += "  \"schema\": \"mnemos.rendered_audio_trace/1\",\n";
        trace += "  \"requested_frames\": " + std::to_string(frames) + ",\n";
        trace += "  \"captured_frames\": " + std::to_string(pcm.size() / 2U) + ",\n";
        trace += "  \"sample_rate\": " + std::to_string(sample_rate) + ",\n";
        trace += "  \"frame_metrics\": [";
        trace += frame_json;
        trace += frame_json.empty() ? "]\n}\n" : "\n  ]\n}\n";
        const std::string trace_path = base_path + ".rendered_audio.json";
        const std::span<const std::uint8_t> trace_bytes(
            reinterpret_cast<const std::uint8_t*>(trace.data()), trace.size());
        if (!io::write_file(trace_path, trace_bytes)) {
            std::fprintf(stderr, "[audio_export] could not write %s\n", trace_path.c_str());
        }
        uninstall_runtime_audio_write_traces(write_targets);
        return pcm.size() / 2U; // (L,R) frames captured
    }

} // namespace mnemos::debug
