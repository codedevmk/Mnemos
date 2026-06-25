#include "headless_commands.hpp"

#include "animation_export.hpp"
#include "asset_export.hpp"
#include "audio_export.hpp"
#include "capability_discovery.hpp"
#include "debug_dump.hpp"
#include "player_system.hpp"
#include "state_file.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

    [[nodiscard]] std::vector<std::uint64_t> parse_swap_frames(int argc, char* argv[]) {
        std::vector<std::uint64_t> frames;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--swap-disk") {
                frames.push_back(std::strtoull(argv[i + 1], nullptr, 10));
            }
        }
        return frames;
    }

    void apply_disk_swaps(mnemos::frontend_sdk::player_system& sys,
                          const std::vector<std::uint64_t>& frames, std::uint64_t frame) {
        if (sys.media_count() <= 1U) {
            return;
        }
        for (const std::uint64_t f : frames) {
            if (f == frame) {
                sys.insert_media((sys.current_media_index() + 1U) % sys.media_count());
            }
        }
    }

    [[nodiscard]] bool tracing_enabled() {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* trace_env = std::getenv("MNEMOS_CPU_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return trace_env != nullptr && trace_env[0] != '\0' && trace_env[0] != '0';
    }

    [[nodiscard]] std::uint64_t headless_progress_interval() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* progress_env = std::getenv("MNEMOS_HEADLESS_PROGRESS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (progress_env == nullptr || progress_env[0] == '\0' || progress_env[0] == '0') {
            return 0U;
        }
        return std::strtoull(progress_env, nullptr, 10);
    }

    void log_headless_progress(const char* label, std::uint64_t frame, std::uint64_t total,
                               std::uint64_t interval) noexcept {
        if (interval == 0U || frame == 0U) {
            return;
        }
        if (frame != 1U && frame != total && (frame % interval) != 0U) {
            return;
        }
        std::fprintf(stderr, "[mnemos_player] %s progress: %llu/%llu frames\n", label,
                     static_cast<unsigned long long>(frame),
                     static_cast<unsigned long long>(total));
        std::fflush(stderr);
    }

    class headless_breadcrumb_session final {
      public:
        headless_breadcrumb_session(const char* label, std::uint64_t total) noexcept
            : label_(label), total_(total), file_(open_file()) {
            mark("start", 0U);
        }

        headless_breadcrumb_session(const headless_breadcrumb_session&) = delete;
        headless_breadcrumb_session& operator=(const headless_breadcrumb_session&) = delete;

        ~headless_breadcrumb_session() {
            mark("end", last_frame_);
            if (file_ != nullptr) {
                std::fclose(file_);
            }
        }

        void mark(const char* phase, std::uint64_t frame) noexcept {
            last_frame_ = frame;
            if (file_ == nullptr) {
                return;
            }
            std::fprintf(file_, "%s,%llu,%llu,%s\n", label_,
                         static_cast<unsigned long long>(frame),
                         static_cast<unsigned long long>(total_), phase);
            std::fflush(file_);
        }

      private:
        [[nodiscard]] static std::FILE* open_file() noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            const char* path = std::getenv("MNEMOS_HEADLESS_BREADCRUMB");
            std::FILE* file = (path != nullptr && path[0] != '\0') ? std::fopen(path, "wb") : nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (path != nullptr && path[0] != '\0' && file == nullptr) {
                std::fprintf(stderr, "[mnemos_player] could not open breadcrumb file: %s\n", path);
                std::fflush(stderr);
            }
            return file;
        }

        const char* label_{};
        std::uint64_t total_{};
        std::uint64_t last_frame_{};
        std::FILE* file_{};
    };

    void apply_press_events(mnemos::frontend_sdk::player_system& sys,
                            const std::vector<mnemos::apps::player::adapters::press_event>& events,
                            std::uint64_t frame) {
        if (events.empty()) {
            return;
        }
        std::uint32_t max_port = 0U;
        for (const auto& event : events) {
            if (event.port_index > max_port) {
                max_port = event.port_index;
            }
        }
        for (std::uint32_t port = 0; port <= max_port; ++port) {
            sys.apply_input(static_cast<int>(port),
                            mnemos::apps::player::adapters::input_for_frame(events, frame, port));
        }
    }

    [[nodiscard]] const char*
    load_status_name(mnemos::runtime::load_status status) noexcept {
        switch (status) {
        case mnemos::runtime::load_status::ok:
            return "ok";
        case mnemos::runtime::load_status::bad_magic:
            return "bad_magic";
        case mnemos::runtime::load_status::unsupported_version:
            return "unsupported_version";
        case mnemos::runtime::load_status::manifest_mismatch:
            return "manifest_mismatch";
        case mnemos::runtime::load_status::truncated:
            return "truncated";
        case mnemos::runtime::load_status::bad_crc:
            return "bad_crc";
        case mnemos::runtime::load_status::decompress_failed:
            return "decompress_failed";
        case mnemos::runtime::load_status::chunk_rejected:
            return "chunk_rejected";
        }
        return "unknown";
    }

} // namespace

namespace mnemos::apps::player {

    bool has_headless_request(const headless_requests& requests) noexcept {
        return requests.capabilities || requests.screenshot || requests.save_state ||
               requests.extract_assets || requests.extract_audio || requests.record_animation;
    }

    std::optional<int> run_headless_request(frontend_sdk::player_system* system,
                                            const headless_requests& requests, int argc,
                                            char* argv[]) {
        using adapters::input_for_frame;
        using adapters::load_save_state_file;
        using adapters::parse_press_events;
        using adapters::save_save_state_file;

        if (requests.load_state) {
            if (system == nullptr) {
                std::fprintf(stderr, "--load-state requires --rom\n");
                return 1;
            }
            if (!system->session_capabilities().save_state_supported) {
                std::fprintf(stderr, "[mnemos_player] system does not support save states\n");
                return 1;
            }
            const auto bytes = load_save_state_file(*requests.load_state);
            if (!bytes.has_value()) {
                std::fprintf(stderr, "[mnemos_player] could not read save state: %s\n",
                             requests.load_state->c_str());
                return 1;
            }
            const auto result = system->load_state(*bytes);
            if (!result.ok()) {
                std::fprintf(stderr, "[mnemos_player] could not load save state %s: %s\n",
                             requests.load_state->c_str(), load_status_name(result.status));
                return 1;
            }
            std::fprintf(stderr, "[mnemos_player] loaded save state: %s\n",
                         requests.load_state->c_str());
            std::fflush(stderr);
        }

        if (requests.capabilities) {
            if (system == nullptr) {
                std::fprintf(stderr, "--capabilities requires --rom\n");
                return 1;
            }
            const auto manifest = debug::discover_dump_capabilities(*system);
            const std::string summary = debug::format_capability_summary(manifest);
            std::fwrite(summary.data(), 1U, summary.size(), stdout);
            std::fflush(stdout);
            return 0;
        }

        if (requests.save_state) {
            if (system == nullptr) {
                std::fprintf(stderr, "--save-state requires --rom\n");
                return 1;
            }
            if (!system->session_capabilities().save_state_supported) {
                std::fprintf(stderr, "[mnemos_player] system does not support save states\n");
                return 1;
            }
            const auto press_events = parse_press_events(argc, argv);
            const auto swap_frames = parse_swap_frames(argc, argv);
            const std::uint64_t progress_interval = headless_progress_interval();
            headless_breadcrumb_session breadcrumb("save-state", requests.save_state->frames);
            for (std::uint64_t i = 0; i < requests.save_state->frames; ++i) {
                const std::uint64_t frame = i + 1U;
                breadcrumb.mark("before_input", frame);
                if (!press_events.empty()) {
                    system->apply_input(0, input_for_frame(press_events, frame));
                }
                apply_disk_swaps(*system, swap_frames, frame);
                breadcrumb.mark("before_step", frame);
                system->step_one_frame();
                breadcrumb.mark("after_step", frame);
                log_headless_progress("save-state", frame, requests.save_state->frames,
                                      progress_interval);
            }
            breadcrumb.mark("before_write_state", requests.save_state->frames);
            const std::vector<std::uint8_t> state = system->save_state();
            if (state.empty() || !save_save_state_file(requests.save_state->path, state)) {
                breadcrumb.mark("write_state_failed", requests.save_state->frames);
                std::fprintf(stderr, "[mnemos_player] could not write save state: %s\n",
                             requests.save_state->path.c_str());
                return 1;
            }
            breadcrumb.mark("after_write_state", requests.save_state->frames);
            std::fprintf(stderr, "[mnemos_player] wrote save state: %s (%zu bytes after %llu "
                                 "frames)\n",
                         requests.save_state->path.c_str(), state.size(),
                         static_cast<unsigned long long>(requests.save_state->frames));
            std::fflush(stderr);
            return 0;
        }

        if (requests.screenshot) {
            if (system == nullptr) {
                std::fprintf(stderr, "--screenshot requires --rom\n");
                return 1;
            }

            std::uint64_t trace_frame = 0;
            const std::string trace_path = requests.screenshot->path + ".cpu_trace.csv";
            std::optional<debug::trace_csv_session> trace;
            if (tracing_enabled()) {
                trace.emplace(*system, trace_path, trace_frame);
            }

            const auto press_events = parse_press_events(argc, argv);
            const auto swap_frames = parse_swap_frames(argc, argv);
            const std::uint64_t progress_interval = headless_progress_interval();
            headless_breadcrumb_session breadcrumb("screenshot", requests.screenshot->frames);
            for (std::uint64_t i = 0; i < requests.screenshot->frames; ++i) {
                const std::uint64_t frame = i + 1U;
                trace_frame = frame;
                breadcrumb.mark("before_input", frame);
                apply_press_events(*system, press_events, frame);
                apply_disk_swaps(*system, swap_frames, frame);
                breadcrumb.mark("before_step", frame);
                system->step_one_frame();
                breadcrumb.mark("after_step", frame);
                log_headless_progress("screenshot", frame, requests.screenshot->frames,
                                      progress_interval);
            }

            breadcrumb.mark("before_dump", requests.screenshot->frames);
            if (!debug::dump_screenshot_artifacts(*system, requests.screenshot->path)) {
                breadcrumb.mark("dump_failed", requests.screenshot->frames);
                std::fprintf(stderr, "could not write screenshot: %s\n",
                             requests.screenshot->path.c_str());
                return 1;
            }
            breadcrumb.mark("after_dump", requests.screenshot->frames);
            const auto fb = system->current_frame();
            std::fprintf(stderr, "[mnemos_player] wrote %s (%ux%u after %llu frames)\n",
                         requests.screenshot->path.c_str(), fb.width, fb.height,
                         static_cast<unsigned long long>(requests.screenshot->frames));
            if (trace && trace->active()) {
                std::fprintf(stderr, "[mnemos_player] wrote %s\n", trace_path.c_str());
                if (trace->trace_count() > 1U) {
                    std::fprintf(stderr, "[mnemos_player] wrote %zu CPU trace files\n",
                                 trace->trace_count());
                }
            }
            std::fflush(stderr);
            return 0;
        }

        if (requests.record_animation) {
            if (system == nullptr) {
                std::fprintf(stderr, "--record-gif/--record-movie requires --rom\n");
                return 1;
            }
            const auto press_events = parse_press_events(argc, argv);
            std::vector<debug::animation_frame> frames;
            frames.reserve(static_cast<std::size_t>(requests.record_animation->frames));
            for (std::uint64_t i = 0; i < requests.record_animation->frames; ++i) {
                apply_press_events(*system, press_events, i + 1U);
                system->step_one_frame();
                auto frame = debug::capture_animation_frame(*system);
                if (!frame) {
                    std::fprintf(stderr, "[mnemos_player] could not capture frame %llu\n",
                                 static_cast<unsigned long long>(i + 1U));
                    return 1;
                }
                frames.push_back(std::move(*frame));
            }

            const std::uint32_t fps_x1000 = system->region().frames_per_second_x1000;
            if (requests.record_animation->format == adapters::animation_record_format::gif) {
                if (!debug::write_gif_animation(requests.record_animation->output, frames,
                                                fps_x1000)) {
                    std::fprintf(stderr, "[mnemos_player] could not write animated GIF: %s\n",
                                 requests.record_animation->output.c_str());
                    return 1;
                }
                std::fprintf(stderr, "[mnemos_player] wrote animated GIF %s (%llu frames)\n",
                             requests.record_animation->output.c_str(),
                             static_cast<unsigned long long>(requests.record_animation->frames));
            } else {
                const auto result =
                    debug::write_movie_frame_sequence(requests.record_animation->output, frames,
                                                      fps_x1000);
                if (result.frames_written != frames.size()) {
                    std::fprintf(stderr,
                                 "[mnemos_player] incomplete movie frame sequence: %zu/%zu "
                                 "frames\n",
                                 result.frames_written, frames.size());
                    return 1;
                }
                std::fprintf(stderr,
                             "[mnemos_player] wrote movie frame sequence %s.* (%zu frames, "
                             "manifest %s)\n",
                             requests.record_animation->output.c_str(), result.frames_written,
                             result.manifest_path.c_str());
            }
            std::fflush(stderr);
            return 0;
        }

        if (requests.extract_assets) {
            if (system == nullptr) {
                std::fprintf(stderr, "--extract-assets requires --rom\n");
                return 1;
            }
            const auto press_events = parse_press_events(argc, argv);
            for (std::uint64_t i = 0; i < requests.extract_assets->frames; ++i) {
                apply_press_events(*system, press_events, i + 1U);
                system->step_one_frame();
            }
            const std::size_t count = debug::export_assets(*system, requests.extract_assets->base);
            std::fprintf(stderr,
                         "[mnemos_player] extracted %zu asset image(s) to %s.* after %llu "
                         "frames\n",
                         count, requests.extract_assets->base.c_str(),
                         static_cast<unsigned long long>(requests.extract_assets->frames));
            std::fflush(stderr);
            return 0;
        }

        if (requests.extract_audio) {
            if (system == nullptr) {
                std::fprintf(stderr, "--extract-audio requires --rom\n");
                return 1;
            }
            const auto press_events = parse_press_events(argc, argv);
            const auto swap_frames = parse_swap_frames(argc, argv);
            const std::size_t rendered =
                debug::export_rendered_audio(*system, requests.extract_audio->frames,
                                             requests.extract_audio->base, [&](std::uint64_t i) {
                                                 apply_press_events(*system, press_events, i + 1U);
                                                 apply_disk_swaps(*system, swap_frames, i + 1U);
                                             });
            const std::size_t count = debug::export_audio(*system, requests.extract_audio->base);
            std::fprintf(stderr,
                         "[mnemos_player] extracted %zu stored sample(s) + %zu rendered frame(s) "
                         "to %s.* after %llu frames\n",
                         count, rendered, requests.extract_audio->base.c_str(),
                         static_cast<unsigned long long>(requests.extract_audio->frames));
            std::fflush(stderr);
            return 0;
        }

        return std::nullopt;
    }

} // namespace mnemos::apps::player
