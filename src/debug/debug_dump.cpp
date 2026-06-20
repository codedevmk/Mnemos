#include "debug_dump.hpp"

#include "file.hpp"
#include "introspection_views.hpp"
#include "path_id.hpp"
#include "png_image.hpp"
#include "ppm_image.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::debug {

    namespace {

        // True if `path` ends in ".png" (case-insensitive).
        [[nodiscard]] bool has_png_extension(std::string_view path) noexcept {
            if (path.size() < 4U) {
                return false;
            }
            const std::string_view ext = path.substr(path.size() - 4U);
            const auto lower = [](char c) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            };
            return ext[0] == '.' && lower(ext[1]) == 'p' && lower(ext[2]) == 'n' &&
                   lower(ext[3]) == 'g';
        }

        // Write a framebuffer as an image, choosing the encoder by the path's
        // extension: ".png" emits a PNG, anything else a PPM (the default for the
        // diagnostic sidecars, which name themselves ".ppm").
        bool dump_framebuffer_image(const chips::frame_buffer_view& fb, const std::string& path) {
            if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
                return false;
            }
            // The framebuffer may be strided (storage pitch > visible width);
            // pack the visible region row-by-row before encoding.
            const std::uint32_t stride = fb.effective_stride();
            std::vector<std::uint32_t> packed;
            packed.reserve(static_cast<std::size_t>(fb.width) * fb.height);
            for (std::uint32_t y = 0; y < fb.height; ++y) {
                const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
                packed.insert(packed.end(), row, row + fb.width);
            }
            if (has_png_extension(path)) {
                const graphics::images::png_image img(fb.width, fb.height, std::move(packed));
                return img.write(path);
            }
            const graphics::images::ppm_image img(fb.width, fb.height, std::move(packed));
            return img.write(path);
        }

        bool dump_bytes(const std::string& path, std::span<const std::uint8_t> bytes) {
            return io::write_file(path, bytes);
        }

        [[nodiscard]] std::string_view
        register_format_name(chips::register_value_format format) noexcept {
            using fmt = chips::register_value_format;
            switch (format) {
            case fmt::unsigned_integer:
                return "unsigned";
            case fmt::signed_integer:
                return "signed";
            case fmt::flags:
                return "flags";
            }
            return "unknown";
        }

        void append_hex(std::string& out, std::uint64_t value, std::uint8_t bit_width) {
            static constexpr char hex[] = "0123456789ABCDEF";
            const unsigned digits = std::max(1U, (static_cast<unsigned>(bit_width) + 3U) / 4U);
            if (bit_width > 0U && bit_width < 64U) {
                value &= (std::uint64_t{1} << bit_width) - 1U;
            }
            for (unsigned i = 0; i < digits; ++i) {
                const unsigned shift = (digits - 1U - i) * 4U;
                out.push_back(hex[(value >> shift) & 0x0FU]);
            }
        }

        bool dump_registers(const std::string& path, instrumentation::register_view& registers) {
            std::string out = "# mnemos register dump v1\n";
            out += "# name bits format value\n";
            for (const chips::register_descriptor& reg : registers.registers()) {
                out += std::string(reg.name);
                out += " bits=";
                out += std::to_string(reg.bit_width);
                out += " format=";
                out += std::string(register_format_name(reg.format));
                out += " value=0x";
                append_hex(out, reg.value, reg.bit_width);
                out += '\n';
            }
            const std::span<const std::uint8_t> bytes(
                reinterpret_cast<const std::uint8_t*>(out.data()), out.size());
            return dump_bytes(path, bytes);
        }

        [[nodiscard]] std::string secondary_trace_path(const std::string& csv_path,
                                                       std::string_view chip_id,
                                                       std::size_t ordinal) {
            const std::string suffix =
                "." + std::string(chip_id) + "." + std::to_string(ordinal) + ".csv";
            static constexpr std::string_view csv_suffix = ".csv";
            if (csv_path.size() >= csv_suffix.size() &&
                std::string_view(csv_path).substr(csv_path.size() - csv_suffix.size()) ==
                    csv_suffix) {
                return csv_path.substr(0U, csv_path.size() - csv_suffix.size()) + suffix;
            }
            return csv_path + suffix;
        }

    } // namespace

    bool dump_screenshot_artifacts(const frontend_sdk::player_system& sys,
                                   const std::string& base_path) {
        if (!dump_framebuffer_image(sys.current_frame(), base_path)) {
            return false;
        }

        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr) {
                continue;
            }
            const std::string chip_id = sanitize_id(chip->metadata().part_number);
            auto& intro = chip->introspection();

            for (instrumentation::memory_view* mv : intro.memory_views()) {
                if (mv == nullptr) {
                    continue;
                }
                const std::string path =
                    base_path + "." + chip_id + "." + sanitize_id(mv->name()) + ".bin";
                if (!dump_bytes(path, mv->bytes())) {
                    std::fprintf(stderr, "[debug_dump] could not write %s\n", path.c_str());
                }
            }

            if (auto* registers = intro.registers()) {
                const std::string path = base_path + "." + chip_id + ".regs.txt";
                if (!dump_registers(path, *registers)) {
                    std::fprintf(stderr, "[debug_dump] could not write %s\n", path.c_str());
                }
            }

            for (instrumentation::debug_layer* layer : intro.debug_layers()) {
                if (layer == nullptr) {
                    continue;
                }
                const std::string path =
                    base_path + "." + chip_id + "." + sanitize_id(layer->name()) + ".ppm";
                if (!dump_framebuffer_image(layer->view(), path)) {
                    std::fprintf(stderr, "[debug_dump] could not write %s\n", path.c_str());
                }
            }
        }

        // System-level memories not owned by any single chip (68K/Z80 work RAM,
        // ...). No chip-id segment, so these land as `<base>.<name>.bin` beside
        // the per-chip `<base>.<chip>.<name>.bin` dumps.
        for (instrumentation::memory_view* mv : sys.memory_views()) {
            if (mv == nullptr) {
                continue;
            }
            const std::string path = base_path + "." + sanitize_id(mv->name()) + ".bin";
            if (!dump_bytes(path, mv->bytes())) {
                std::fprintf(stderr, "[debug_dump] could not write %s\n", path.c_str());
            }
        }

        return true;
    }

    struct trace_csv_session::state final {
        std::ofstream* out{};
        const std::uint64_t* frame{};
        std::uint64_t inst{};
    };

    struct trace_csv_session::sink final {
        chips::ichip* target{};
        std::ofstream out{};
        std::unique_ptr<state> cb_state{};
    };

    trace_csv_session::trace_csv_session(frontend_sdk::player_system& sys,
                                         const std::string& csv_path,
                                         const std::uint64_t& frame_counter) {
        std::size_t trace_index = 0;
        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr) {
                continue;
            }
            auto* trace = chip->introspection().trace();
            if (trace == nullptr) {
                continue;
            }

            const std::string path =
                trace_index == 0U
                    ? csv_path
                    : secondary_trace_path(csv_path, sanitize_id(chip->metadata().part_number),
                                           trace_index);
            auto sink = std::make_unique<trace_csv_session::sink>();
            sink->target = chip;
            sink->out.open(path);
            if (!sink->out) {
                ++trace_index;
                continue;
            }
            sink->out << "frame,inst,pc,cycles\n";

            // Allocate per-sink callback state on the heap so captured pointers
            // remain stable even as the sink list grows.
            sink->cb_state = std::make_unique<state>();
            sink->cb_state->out = &sink->out;
            sink->cb_state->frame = &frame_counter;
            sink->cb_state->inst = 0;

            state* s = sink->cb_state.get();
            trace->install([s](const instrumentation::trace_event& ev) {
                char buf[80];
                const std::uint64_t frame = s->frame != nullptr ? *s->frame : 0U;
                std::snprintf(buf, sizeof(buf), "%llu,%llu,%06X,%llu\n",
                              static_cast<unsigned long long>(frame),
                              static_cast<unsigned long long>(s->inst),
                              static_cast<unsigned int>(ev.pc),
                              static_cast<unsigned long long>(ev.cycles));
                *s->out << buf;
                ++s->inst;
            });
            sinks_.push_back(std::move(sink));
            ++trace_index;
        }
    }

    trace_csv_session::~trace_csv_session() {
        for (const auto& sink : sinks_) {
            if (sink != nullptr && sink->target != nullptr) {
                if (auto* trace = sink->target->introspection().trace()) {
                    trace->install({});
                }
            }
        }
    }

} // namespace mnemos::debug
