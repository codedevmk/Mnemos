#include "audio_export.hpp"

#include "audio_views.hpp"
#include "file.hpp"
#include "json_util.hpp"
#include "path_id.hpp"
#include "wav.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace mnemos::debug {

    std::size_t export_audio(const frontend_sdk::player_system& sys, const std::string& base_path) {
        std::size_t written = 0;
        std::string json = "{\n  \"chips\": [";
        bool first_chip = true;

        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr) {
                continue;
            }
            instrumentation::audio_source* src = chip->introspection().audio();
            if (src == nullptr) {
                continue;
            }
            const std::string chip_id = sanitize_id(chip->metadata().part_number);
            const std::span<const instrumentation::sample_view> samples = src->samples();

            json += first_chip ? "\n" : ",\n";
            first_chip = false;
            json += "    {\n      \"id\": " + json_string(chip_id) +
                    ",\n      \"part_number\": " + json_string(chip->metadata().part_number) +
                    ",\n      \"samples\": [";

            for (std::size_t i = 0; i < samples.size(); ++i) {
                const instrumentation::sample_view& s = samples[i];
                const std::string path =
                    base_path + "." + chip_id + ".sample." + std::string(s.name) + ".wav";
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
            json += samples.empty() ? "]\n    }" : "\n      ]\n    }";
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

} // namespace mnemos::debug
