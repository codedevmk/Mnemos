#include "c64_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"
#include "file.hpp"   // mnemos::io::read_file
#include "string.hpp" // mnemos::common::to_lower

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using mnemos::dsp::clip_i16;
using mnemos::dsp::kMixerGainOne;
using mnemos::dsp::kOutputRate;
using mnemos::dsp::sample_channel_box;
using mnemos::dsp::scale_q12;

namespace mnemos::apps::player::adapters::c64 {

    namespace {

        // Scheduler order comes straight from the manifest runtime
        // (c64_runtime::schedule(): VIC, CPU, CIA1, CIA2, SID, drive, tape, all
        // φ2). This just lowers that tier-neutral view into runtime::scheduled_chip.
        std::vector<runtime::scheduled_chip> build_schedule(manifests::c64::c64_runtime& sys) {
            std::vector<runtime::scheduled_chip> chips;
            for (const auto& e : sys.schedule()) {
                chips.push_back({e.chip, e.weight});
            }
            return chips;
        }

        [[nodiscard]] mnemos::video_region to_video_region(manifests::c64::c64_config::region r) {
            return r == manifests::c64::c64_config::region::ntsc ? mnemos::video_region::ntsc
                                                                 : mnemos::video_region::pal;
        }

        // The SID's sample() is already a fully mixed, volume-scaled int16, so
        // it mixes at unity -- the system-agnostic DSP helpers live in mnemos::dsp.
        constexpr int kGainSid = kMixerGainOne;

    } // namespace

    c64_adapter::c64_adapter(std::vector<std::uint8_t> basic_rom,
                             std::vector<std::uint8_t> kernal_rom,
                             std::vector<std::uint8_t> chargen_rom,
                             const manifests::c64::c64_config& config, std::string display_name,
                             frontend_sdk::scheduler_factory* scheduler_factory)
        : sys_(manifests::c64::build_c64_runtime(std::move(basic_rom), std::move(kernal_rom),
                                                 std::move(chargen_rom), config)),
          scheduler_(frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_),
                                                  sys_->video())),
          region_(to_video_region(config.video_region)),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(region_)]) {
        sys_->sid->enable_audio_capture(true);

        // Non-owning chip enumeration in scheduler order; matches build_schedule().
        chip_view_[0] = sys_->video();
        chip_view_[1] = sys_->cpu();
        chip_view_[2] = sys_->cia1;
        chip_view_[3] = sys_->cia2;
        chip_view_[4] = sys_->sid;

        // Publish the static description once, post-init.
        spec_.push_back({.label = "System", .value = "Commodore 64"});
        spec_.push_back(
            {.label = "Region", .value = region_ == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Media", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region c64_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view c64_adapter::current_frame() const noexcept {
        return sys_->video()->framebuffer();
    }

    void c64_adapter::step_one_frame() {
        scheduler_.run_frame();
        ++frames_stepped_;
    }

    void c64_adapter::apply_input(int port, const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(ports_.size())) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        // Map the abstract pad onto a C64 digital joystick. Player 0 drives
        // control port 2 (the conventional game port), player 1 drives port 1.
        // The matrix the CIA1 read callbacks resolve folds the joystick over
        // the keyboard lines, active-low; c64_input::set_joystick handles that.
        const std::uint8_t c64_port = port == 0 ? 2U : 1U;
        using in = manifests::c64::c64_input;
        std::uint8_t mask = 0U;
        if (state.up) {
            mask |= in::joy_up;
        }
        if (state.down) {
            mask |= in::joy_down;
        }
        if (state.left) {
            mask |= in::joy_left;
        }
        if (state.right) {
            mask |= in::joy_right;
        }
        if (state.a || state.b) {
            mask |= in::joy_fire;
        }
        sys_->input.set_joystick(c64_port, mask);
    }

    frontend_sdk::audio_chunk c64_adapter::drain_audio() noexcept {
        const std::size_t sid_count = sys_->sid->pending_samples();
        if (sid_count == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }
        sid_buf_.resize(sid_count);
        sys_->sid->drain_samples(sid_buf_.data(), sid_count);

        // Accumulate the fractional sample so the long-term output rate is
        // exact even when (kOutputRate / target_fps_) is not an integer.
        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        const double sid_scale = static_cast<double>(sid_count) / static_cast<double>(dst_pairs);
        for (int i = 0; i < dst_pairs; ++i) {
            const int s = sample_channel_box(sid_buf_.data(), 1, 0, static_cast<int>(sid_count),
                                             sid_scale * i, sid_scale * (i + 1));
            const std::int16_t out = clip_i16(scale_q12(s, kGainSid));
            mix_buf_[i * 2 + 0] = out;
            mix_buf_[i * 2 + 1] = out; // duplicate mono into both stereo lanes
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

    void force_link() noexcept {}

    namespace {

        // Find a system ROM image in `dir` by case-insensitive filename
        // substring (`role`) and exact size. Mirrors the manifest parity test's
        // ROM-set convention (basic/kernal/chargen, 8K/8K/4K). File reading and
        // ASCII case-folding reuse the shared mnemos::io / mnemos::common helpers.
        std::optional<std::vector<std::uint8_t>> find_system_rom(const std::filesystem::path& dir,
                                                                 std::string_view role,
                                                                 std::size_t expected_size) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                if (mnemos::common::to_lower(entry.path().filename().string()).find(role) ==
                    std::string::npos) {
                    continue;
                }
                auto bytes = mnemos::io::read_file(entry.path().string());
                if (bytes && bytes->size() == expected_size) {
                    return bytes;
                }
            }
            return std::nullopt;
        }

        // Load the BASIC/KERNAL/CHARGEN set from $MNEMOS_C64_ROM_DIR. Any image
        // that can't be found is substituted with a correctly-sized zero buffer
        // so the player still launches without a ROM set (boots a blank machine,
        // exactly as the manifest parity test's synthetic path does).
        struct system_roms final {
            std::vector<std::uint8_t> basic;
            std::vector<std::uint8_t> kernal;
            std::vector<std::uint8_t> chargen;
        };

        system_roms load_system_roms() {
            constexpr std::size_t kBasicSize = 0x2000U;
            constexpr std::size_t kKernalSize = 0x2000U;
            constexpr std::size_t kChargenSize = 0x1000U;
            system_roms roms{.basic = std::vector<std::uint8_t>(kBasicSize, 0x00U),
                             .kernal = std::vector<std::uint8_t>(kKernalSize, 0x00U),
                             .chargen = std::vector<std::uint8_t>(kChargenSize, 0x00U)};
            const char* dir = std::getenv("MNEMOS_C64_ROM_DIR"); // NOLINT(concurrency-mt-unsafe)
            if (dir == nullptr || dir[0] == '\0') {
                return roms;
            }
            const std::filesystem::path root(dir);
            if (auto b = find_system_rom(root, "basic", kBasicSize)) {
                roms.basic = std::move(*b);
            }
            if (auto k = find_system_rom(root, "kernal", kKernalSize)) {
                roms.kernal = std::move(*k);
            }
            if (auto c = find_system_rom(root, "char", kChargenSize)) {
                roms.chargen = std::move(*c);
            }
            return roms;
        }

        const auto register_c64 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "c64",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    // The C64 boots from its system ROMs, not from opts.rom (the
                    // selected file): autostarting a .prg/.d64 program is a
                    // separate media-load feature the manifest tier does not yet
                    // expose. opts.display_name still surfaces the file as the
                    // "Media" spec row.
                    auto roms = load_system_roms();
                    const auto region = opts.video_region == mnemos::video_region::ntsc
                                            ? manifests::c64::c64_config::region::ntsc
                                            : manifests::c64::c64_config::region::pal;
                    auto* sched_factory = opts.scheduler_factory_override;
                    return std::make_unique<c64_adapter>(
                        std::move(roms.basic), std::move(roms.kernal), std::move(roms.chargen),
                        manifests::c64::c64_config{.video_region = region},
                        std::move(opts.display_name), sched_factory);
                });
            return 0;
        }();

    } // namespace

} // namespace mnemos::apps::player::adapters::c64
