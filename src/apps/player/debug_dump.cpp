#include "debug_dump.hpp"

#include "introspection_views.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace mnemos::apps::player {

    namespace {

        std::string sanitize_id(std::string_view raw) {
            std::string out;
            out.reserve(raw.size());
            for (char c : raw) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                } else {
                    out.push_back('_');
                }
            }
            return out;
        }

        bool dump_framebuffer_ppm(const chips::frame_buffer_view& fb, const std::string& path) {
            if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
                return false;
            }
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            const std::uint32_t stride = fb.effective_stride();
            out << "P6\n" << fb.width << " " << fb.height << "\n255\n";
            for (std::uint32_t y = 0; y < fb.height; ++y) {
                const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
                for (std::uint32_t x = 0; x < fb.width; ++x) {
                    const std::uint32_t p = row[x];
                    const char rgb[3] = {static_cast<char>((p >> 16U) & 0xFFU),
                                         static_cast<char>((p >> 8U) & 0xFFU),
                                         static_cast<char>(p & 0xFFU)};
                    out.write(rgb, 3);
                }
            }
            return out.good();
        }

        bool dump_bytes(const std::string& path, std::span<const std::uint8_t> bytes) {
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            return out.good();
        }

    } // namespace

    bool dump_screenshot_artifacts(const frontend_sdk::player_system& sys,
                                   const std::string& base_path) {
        if (!dump_framebuffer_ppm(sys.current_frame(), base_path)) {
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
                    base_path + "." + chip_id + "." + std::string(mv->name()) + ".bin";
                if (!dump_bytes(path, mv->bytes())) {
                    std::fprintf(stderr, "[debug_dump] could not write %s\n", path.c_str());
                }
            }

            for (instrumentation::debug_layer* layer : intro.debug_layers()) {
                if (layer == nullptr) {
                    continue;
                }
                const std::string path =
                    base_path + "." + chip_id + "." + std::string(layer->name()) + ".ppm";
                if (!dump_framebuffer_ppm(layer->view(), path)) {
                    std::fprintf(stderr, "[debug_dump] could not write %s\n", path.c_str());
                }
            }
        }

        return true;
    }

    struct trace_csv_session::state final {
        std::ofstream* out{};
        const std::uint64_t* frame{};
        std::uint64_t inst{};
    };

    trace_csv_session::trace_csv_session(frontend_sdk::player_system& sys,
                                         const std::string& csv_path,
                                         const std::uint64_t& frame_counter) {
        // Find the first chip with a trace_target. Adapters publish chips in
        // scheduler order, so this is typically the primary CPU.
        chips::ichip* candidate{};
        instrumentation::trace_target* tt{};
        for (chips::ichip* chip : sys.chips()) {
            if (chip == nullptr) {
                continue;
            }
            if (auto* t = chip->introspection().trace()) {
                candidate = chip;
                tt = t;
                break;
            }
        }
        if (tt == nullptr) {
            return;
        }
        out_.open(csv_path);
        if (!out_) {
            return;
        }
        out_ << "frame,inst,pc,cycles\n";

        // Allocate per-session callback state on the heap so the captured
        // pointer stays valid even if `this` is moved (it isn't here -- move
        // is deleted -- but the heap split also keeps the lambda small).
        state_ = std::make_unique<state>();
        state_->out = &out_;
        state_->frame = &frame_counter;
        state_->inst = 0;

        target_ = candidate;

        state* s = state_.get();
        tt->install([s](const instrumentation::trace_event& ev) {
            char buf[80];
            const std::uint64_t frame = s->frame != nullptr ? *s->frame : 0U;
            std::snprintf(
                buf, sizeof(buf), "%llu,%llu,%06X,%llu\n", static_cast<unsigned long long>(frame),
                static_cast<unsigned long long>(s->inst), static_cast<unsigned int>(ev.pc),
                static_cast<unsigned long long>(ev.cycles));
            *s->out << buf;
            ++s->inst;
        });
    }

    trace_csv_session::~trace_csv_session() {
        if (target_ != nullptr) {
            if (auto* tt = target_->introspection().trace()) {
                tt->install({});
            }
        }
    }

} // namespace mnemos::apps::player
