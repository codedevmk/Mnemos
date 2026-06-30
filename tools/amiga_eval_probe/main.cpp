#include "amiga_adapter.hpp"
#include "debug_dump.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    namespace fs = std::filesystem;
    namespace amiga = mnemos::apps::player::adapters::amiga;

    struct options final {
        fs::path kickstart;
        std::vector<fs::path> disks;
        fs::path out_dir;
        fs::path trace_path;
        fs::path framebuffer_path;
        fs::path audio_path;
        fs::path metadata_path;
        std::string system{"amiga500"};
        std::string region{"pal"};
        std::uint64_t cycles{};
        std::uint64_t frames{};
        std::size_t fast_ram_size{};
        bool help{};
    };

    [[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
        if (text.empty()) {
            return false;
        }
        const char* first = text.data();
        const char* last = text.data() + text.size();
        std::uint64_t value = 0U;
        const std::from_chars_result result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last) {
            return false;
        }
        out = value;
        return true;
    }

    [[nodiscard]] bool parse_size(std::string_view text, std::size_t& out) noexcept {
        std::uint64_t value = 0U;
        if (!parse_u64(text, value) || value > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        out = static_cast<std::size_t>(value);
        return true;
    }

    [[nodiscard]] std::optional<std::string_view> option_value(std::string_view arg,
                                                               std::string_view name,
                                                               int& index,
                                                               int argc,
                                                               char* argv[]) {
        if (arg == name) {
            if (index + 1 >= argc) {
                return std::nullopt;
            }
            ++index;
            return std::string_view{argv[index]};
        }
        const std::string prefix = std::string(name) + "=";
        if (arg.starts_with(prefix)) {
            return arg.substr(prefix.size());
        }
        return std::nullopt;
    }

    [[nodiscard]] bool parse_args(int argc, char* argv[], options& opts, std::string& error) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            if (arg == "--help" || arg == "-h") {
                opts.help = true;
                return true;
            }

            int value_index = i;
            if (const auto value = option_value(arg, "--kickstart", value_index, argc, argv)) {
                opts.kickstart = fs::path{std::string(*value)};
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--disk", value_index, argc, argv)) {
                opts.disks.emplace_back(std::string(*value));
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--out-dir", value_index, argc, argv)) {
                opts.out_dir = fs::path{std::string(*value)};
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--trace", value_index, argc, argv)) {
                opts.trace_path = fs::path{std::string(*value)};
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--framebuffer", value_index, argc, argv)) {
                opts.framebuffer_path = fs::path{std::string(*value)};
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--audio", value_index, argc, argv)) {
                opts.audio_path = fs::path{std::string(*value)};
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--metadata", value_index, argc, argv)) {
                opts.metadata_path = fs::path{std::string(*value)};
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--system", value_index, argc, argv)) {
                opts.system = std::string(*value);
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--region", value_index, argc, argv)) {
                opts.region = std::string(*value);
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--cycles", value_index, argc, argv)) {
                if (!parse_u64(*value, opts.cycles)) {
                    error = "--cycles requires an unsigned integer";
                    return false;
                }
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--frames", value_index, argc, argv)) {
                if (!parse_u64(*value, opts.frames)) {
                    error = "--frames requires an unsigned integer";
                    return false;
                }
                i = value_index;
                continue;
            }
            value_index = i;
            if (const auto value = option_value(arg, "--fast-ram", value_index, argc, argv)) {
                if (!parse_size(*value, opts.fast_ram_size)) {
                    error = "--fast-ram requires a byte count";
                    return false;
                }
                i = value_index;
                continue;
            }

            error = "unknown option: " + std::string(arg);
            return false;
        }

        if (opts.kickstart.empty()) {
            error = "--kickstart is required";
            return false;
        }
        if (opts.out_dir.empty()) {
            error = "--out-dir is required";
            return false;
        }
        if ((opts.cycles == 0U && opts.frames == 0U) ||
            (opts.cycles != 0U && opts.frames != 0U)) {
            error = "exactly one of --cycles or --frames is required";
            return false;
        }
        return true;
    }

    void print_usage() {
        std::puts(
            "usage: mnemos_amiga_eval_probe --kickstart PATH --out-dir DIR "
            "(--cycles N | --frames N) [--disk PATH ...] [--system amiga500] "
            "[--region pal|ntsc]");
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    read_binary_file(const fs::path& path, std::string& error) {
        std::error_code ec;
        const std::uintmax_t byte_count = fs::file_size(path, ec);
        if (ec) {
            error = "could not stat " + path.string() + ": " + ec.message();
            return std::nullopt;
        }
        if (byte_count > static_cast<std::uintmax_t>(
                             std::numeric_limits<std::size_t>::max())) {
            error = "file is too large: " + path.string();
            return std::nullopt;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_count));
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error = "could not open " + path.string();
            return std::nullopt;
        }
        if (!bytes.empty()) {
            in.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            if (!in) {
                error = "could not read " + path.string();
                return std::nullopt;
            }
        }
        return bytes;
    }

    [[nodiscard]] std::optional<mnemos::manifests::amiga::amiga_model>
    parse_model(std::string_view token) noexcept {
        using model = mnemos::manifests::amiga::amiga_model;
        if (token == "amiga500" || token == "a500") {
            return model::amiga500;
        }
        if (token == "amiga500plus" || token == "a500plus" || token == "amiga500_plus") {
            return model::amiga500_plus;
        }
        if (token == "amiga600" || token == "a600") {
            return model::amiga600;
        }
        if (token == "amiga2000" || token == "a2000") {
            return model::amiga2000;
        }
        if (token == "amiga2000_ecs_1m" || token == "a2000_ecs_1m") {
            return model::amiga2000_ecs_1m;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<mnemos::video_region>
    parse_region(std::string_view token) noexcept {
        if (token == "pal") {
            return mnemos::video_region::pal;
        }
        if (token == "ntsc") {
            return mnemos::video_region::ntsc;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string escape_json(std::string_view text) {
        std::string out;
        out.reserve(text.size() + 8U);
        for (const char c : text) {
            switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20U) {
                    out += "\\u00";
                    constexpr char hex[] = "0123456789ABCDEF";
                    out.push_back(hex[(static_cast<unsigned char>(c) >> 4U) & 0x0FU]);
                    out.push_back(hex[static_cast<unsigned char>(c) & 0x0FU]);
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
        return out;
    }

    [[nodiscard]] bool write_all_bytes(const fs::path& path, std::span<const std::uint8_t> bytes,
                                       std::string& error) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            error = "could not open " + path.string();
            return false;
        }
        if (!bytes.empty()) {
            if (bytes.size() > static_cast<std::size_t>(
                                   std::numeric_limits<std::streamsize>::max())) {
                error = "write is too large: " + path.string();
                return false;
            }
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out) {
                error = "could not write " + path.string();
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool write_audio_dump(const fs::path& path,
                                        mnemos::frontend_sdk::audio_chunk chunk,
                                        std::string& error) {
        if (chunk.samples == nullptr || chunk.frame_count == 0U) {
            return write_all_bytes(path, {}, error);
        }
        const std::size_t sample_count = static_cast<std::size_t>(chunk.frame_count) * 2U;
        const std::size_t byte_count = sample_count * sizeof(std::int16_t);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(chunk.samples);
        return write_all_bytes(path, std::span<const std::uint8_t>(bytes, byte_count), error);
    }

    [[nodiscard]] bool write_metadata(const fs::path& path, const options& opts,
                                      amiga::amiga_adapter& adapter,
                                      double elapsed_seconds,
                                      bool trace_active,
                                      mnemos::frontend_sdk::audio_chunk audio,
                                      std::string& error) {
        const auto fb = adapter.current_frame();
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            error = "could not open " + path.string();
            return false;
        }
        out << "{\n";
        out << "  \"schema\": \"mnemos.amiga.eval_probe/1\",\n";
        out << "  \"system\": \"" << escape_json(opts.system) << "\",\n";
        out << "  \"region\": \"" << escape_json(opts.region) << "\",\n";
        out << "  \"cycles_requested\": " << opts.cycles << ",\n";
        out << "  \"frames_requested\": " << opts.frames << ",\n";
        out << "  \"master_cycle\": " << adapter.scheduler().master_cycle() << ",\n";
        out << "  \"frame_index\": " << adapter.system().frame_index << ",\n";
        out << "  \"elapsed_seconds\": " << elapsed_seconds << ",\n";
        out << "  \"video_fps_x1000\": " << adapter.region().frames_per_second_x1000 << ",\n";
        out << "  \"framebuffer\": {\n";
        out << "    \"path\": \"" << escape_json(opts.framebuffer_path.string()) << "\",\n";
        out << "    \"width\": " << fb.width << ",\n";
        out << "    \"height\": " << fb.height << ",\n";
        out << "    \"stride\": " << fb.effective_stride() << "\n";
        out << "  },\n";
        out << "  \"trace\": {\n";
        out << "    \"path\": \"" << escape_json(opts.trace_path.string()) << "\",\n";
        out << "    \"active\": " << (trace_active ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"audio\": {\n";
        out << "    \"path\": \"" << escape_json(opts.audio_path.string()) << "\",\n";
        out << "    \"sample_rate\": " << audio.sample_rate << ",\n";
        out << "    \"frame_count\": " << audio.frame_count << ",\n";
        out << "    \"format\": \"s16le_stereo\"\n";
        out << "  },\n";
        out << "  \"disks\": " << opts.disks.size() << "\n";
        out << "}\n";
        if (!out) {
            error = "could not write " + path.string();
            return false;
        }
        return true;
    }

} // namespace

int main(int argc, char* argv[]) {
    options opts;
    std::string error;
    if (!parse_args(argc, argv, opts, error)) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] %s\n", error.c_str());
        print_usage();
        return 2;
    }
    if (opts.help) {
        print_usage();
        return 0;
    }

    std::error_code ec;
    fs::create_directories(opts.out_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] could not create %s: %s\n",
                     opts.out_dir.string().c_str(), ec.message().c_str());
        return 1;
    }
    if (opts.trace_path.empty()) {
        opts.trace_path = opts.out_dir / "trace.csv";
    }
    if (opts.framebuffer_path.empty()) {
        opts.framebuffer_path = opts.out_dir / "framebuffer.ppm";
    }
    if (opts.audio_path.empty()) {
        opts.audio_path = opts.out_dir / "audio.s16le";
    }
    if (opts.metadata_path.empty()) {
        opts.metadata_path = opts.out_dir / "metadata.json";
    }

    const auto model = parse_model(opts.system);
    if (!model) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] unsupported system: %s\n",
                     opts.system.c_str());
        return 2;
    }
    const auto region = parse_region(opts.region);
    if (!region) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] unsupported region: %s\n",
                     opts.region.c_str());
        return 2;
    }

    auto kickstart = read_binary_file(opts.kickstart, error);
    if (!kickstart) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] %s\n", error.c_str());
        return 1;
    }
    std::vector<std::vector<std::uint8_t>> disks;
    disks.reserve(opts.disks.size());
    for (const fs::path& disk_path : opts.disks) {
        auto disk = read_binary_file(disk_path, error);
        if (!disk) {
            std::fprintf(stderr, "[mnemos_amiga_eval_probe] %s\n", error.c_str());
            return 1;
        }
        disks.push_back(std::move(*disk));
    }

    mnemos::manifests::amiga::amiga_config config{};
    config.model = *model;
    config.video_region = *region;
    config.fast_ram_size = opts.fast_ram_size;

    amiga::amiga_adapter adapter(std::move(*kickstart), config, opts.system, std::move(disks));
    if (!adapter.scheduler().config_valid()) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] scheduler configuration is invalid\n");
        return 1;
    }

    std::uint64_t trace_frame = adapter.system().frame_index;
    bool trace_active = false;
    const auto started = std::chrono::steady_clock::now();
    {
        mnemos::debug::trace_csv_session trace(adapter, opts.trace_path.string(), trace_frame);
        trace_active = trace.active();
        if (opts.cycles != 0U) {
            std::uint64_t remaining = opts.cycles;
            while (remaining != 0U) {
                const std::uint64_t slice = std::min<std::uint64_t>(remaining, 4096U);
                adapter.scheduler().run_master_cycles(slice);
                remaining -= slice;
                trace_frame = adapter.system().frame_index;
            }
        } else {
            for (std::uint64_t frame = 0U; frame < opts.frames; ++frame) {
                trace_frame = frame + 1U;
                adapter.step_one_frame();
            }
        }
    }
    const auto finished = std::chrono::steady_clock::now();
    const double elapsed_seconds =
        std::chrono::duration<double>(finished - started).count();

    if (!trace_active) {
        static constexpr std::string_view header = "frame,inst,pc,cycles\n";
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(header.data());
        if (!write_all_bytes(opts.trace_path,
                             std::span<const std::uint8_t>(bytes, header.size()), error)) {
            std::fprintf(stderr, "[mnemos_amiga_eval_probe] %s\n", error.c_str());
            return 1;
        }
    }

    if (!mnemos::debug::dump_screenshot_artifacts(adapter, opts.framebuffer_path.string())) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] could not write framebuffer: %s\n",
                     opts.framebuffer_path.string().c_str());
        return 1;
    }

    const mnemos::frontend_sdk::audio_chunk audio = adapter.drain_audio();
    if (!write_audio_dump(opts.audio_path, audio, error)) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] %s\n", error.c_str());
        return 1;
    }

    if (!write_metadata(opts.metadata_path, opts, adapter, elapsed_seconds, trace_active, audio,
                        error)) {
        std::fprintf(stderr, "[mnemos_amiga_eval_probe] %s\n", error.c_str());
        return 1;
    }

    std::fprintf(stderr,
                 "[mnemos_amiga_eval_probe] wrote trace/framebuffer/audio/metadata to %s\n",
                 opts.out_dir.string().c_str());
    return 0;
}
