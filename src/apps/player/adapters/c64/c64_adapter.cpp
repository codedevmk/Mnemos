#include "c64_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"
#include "d64_image.hpp" // c1541::d64_image::size_* (D64 geometry sniff)
#include "file.hpp"      // mnemos::io::read_file
#include "prg_disk.hpp"  // mnemos::chips::storage::c1541::make_prg_disk
#include "string.hpp"    // mnemos::common::to_lower

#include <array>
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

        using key = manifests::c64::c64_input::key;

        // ---- Autostart timing (frames). The KERNAL keyscan runs once per video
        //      frame, so a key is held a few frames then released a few so each
        //      press registers exactly once (short of the auto-repeat delay). ----
        constexpr std::uint32_t kWarmupFrames = 180U; // ~3 s: let the KERNAL reach READY
        constexpr std::uint32_t kHoldFrames = 3U;     // key down
        constexpr std::uint32_t kGapFrames = 3U;      // key up before the next
        constexpr std::uint32_t kChordFrames = kHoldFrames + kGapFrames;
        constexpr std::uint32_t kIdleSettle = 30U;     // drive idle this long -> load done
        constexpr std::uint32_t kAwaitTimeout = 3600U; // give up waiting after ~60 s

        // Scheduler order comes straight from the manifest runtime
        // (c64_runtime::schedule(): VIC, CPU, CIA1, CIA2, SID, drive, tape, all
        // phi2). This just lowers that tier-neutral view into runtime::scheduled_chip.
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

        [[nodiscard]] bool starts_with(std::span<const std::uint8_t> data, std::string_view magic) {
            if (data.size() < magic.size()) {
                return false;
            }
            for (std::size_t i = 0; i < magic.size(); ++i) {
                if (data[i] != static_cast<std::uint8_t>(magic[i])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool is_cartridge(std::span<const std::uint8_t> d) {
            return starts_with(d, "C64 CARTRIDGE   ");
        }
        [[nodiscard]] bool is_tape(std::span<const std::uint8_t> d) {
            return starts_with(d, "C64-TAPE-RAW");
        }
        [[nodiscard]] bool is_d64(std::span<const std::uint8_t> d) {
            namespace c1541 = mnemos::chips::storage::c1541;
            return d.size() == c1541::d64_image::size_35_tracks ||
                   d.size() == c1541::d64_image::size_40_tracks;
        }

        // A display name (path stem) reduced to a PETSCII directory name: ASCII
        // upper-cased, at most 16 chars. Cosmetic -- LOAD"*" finds the file
        // regardless -- so unmapped characters are simply dropped.
        std::vector<std::uint8_t> to_petscii_name(const std::string& display) {
            std::vector<std::uint8_t> out;
            for (char c : display) {
                if (out.size() >= 16U) {
                    break;
                }
                if (c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                }
                const bool keep = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ';
                if (keep) {
                    out.push_back(static_cast<std::uint8_t>(c));
                }
            }
            return out;
        }

        // Turn one media image into a mountable .d64: pass a real disk through,
        // wrap anything else as a single-PRG disk. Empty if it can't be wrapped.
        std::vector<std::uint8_t> as_disk(std::vector<std::uint8_t> image,
                                          const std::vector<std::uint8_t>& name) {
            if (is_d64(image)) {
                return image;
            }
            return mnemos::chips::storage::c1541::make_prg_disk(image, name);
        }

        // Append the chords that type `text` (then RETURN) to `queue`.
        void enqueue_command(std::vector<key_chord>& queue, std::string_view text) {
            for (char c : text) {
                const key_chord chord = ascii_to_chord(c);
                if (chord.count > 0U) {
                    queue.push_back(chord);
                }
            }
            queue.push_back({.keys = {key::ret, key::ret}, .count = 1U});
        }

    } // namespace

    key_chord ascii_to_chord(char c) noexcept {
        const auto one = [](key k) { return key_chord{.keys = {k, k}, .count = 1U}; };
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        switch (c) {
        case 'A':
            return one(key::a);
        case 'B':
            return one(key::b);
        case 'C':
            return one(key::c);
        case 'D':
            return one(key::d);
        case 'E':
            return one(key::e);
        case 'F':
            return one(key::f);
        case 'G':
            return one(key::g);
        case 'H':
            return one(key::h);
        case 'I':
            return one(key::i);
        case 'J':
            return one(key::j);
        case 'K':
            return one(key::k);
        case 'L':
            return one(key::l);
        case 'M':
            return one(key::m);
        case 'N':
            return one(key::n);
        case 'O':
            return one(key::o);
        case 'P':
            return one(key::p);
        case 'Q':
            return one(key::q);
        case 'R':
            return one(key::r);
        case 'S':
            return one(key::s);
        case 'T':
            return one(key::t);
        case 'U':
            return one(key::u);
        case 'V':
            return one(key::v);
        case 'W':
            return one(key::w);
        case 'X':
            return one(key::x);
        case 'Y':
            return one(key::y);
        case 'Z':
            return one(key::z);
        case '0':
            return one(key::k0);
        case '1':
            return one(key::k1);
        case '2':
            return one(key::k2);
        case '3':
            return one(key::k3);
        case '4':
            return one(key::k4);
        case '5':
            return one(key::k5);
        case '6':
            return one(key::k6);
        case '7':
            return one(key::k7);
        case '8':
            return one(key::k8);
        case '9':
            return one(key::k9);
        case '*':
            return one(key::asterisk);
        case ',':
            return one(key::comma);
        case '\r':
        case '\n':
            return one(key::ret);
        case ' ':
            return one(key::space);
        case '"':
            return {.keys = {key::lshift, key::k2}, .count = 2U}; // SHIFT+2
        default:
            return {};
        }
    }

    c64_adapter::c64_adapter(std::vector<std::uint8_t> basic_rom,
                             std::vector<std::uint8_t> kernal_rom,
                             std::vector<std::uint8_t> chargen_rom, std::vector<std::uint8_t> media,
                             std::vector<std::vector<std::uint8_t>> additional_disks,
                             bool autostart, const manifests::c64::c64_config& config,
                             std::string display_name,
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

        // Route the primary media to the device that would carry it, then wire
        // any extra disks for swapping. The KERNAL does the actual loading.
        const auto name = to_petscii_name(display_name);
        if (!media.empty()) {
            if (is_cartridge(media)) {
                kind_ = media_kind::cartridge;
                (void)sys_->cart.load_crt(media); // maps itself; the machine boots into it
            } else if (is_tape(media)) {
                kind_ = media_kind::tape;
                if (sys_->tape.load_tap(media)) {
                    sys_->tape.set_play(true); // satisfy "PRESS PLAY ON TAPE"
                }
            } else {
                kind_ = media_kind::disk;
                if (auto disk = as_disk(std::move(media), name); !disk.empty()) {
                    disks_.push_back(std::move(disk));
                }
            }
        }
        if (kind_ == media_kind::disk) {
            for (auto& extra : additional_disks) {
                if (auto disk = as_disk(std::move(extra), name); !disk.empty()) {
                    disks_.push_back(std::move(disk));
                }
            }
            if (!disks_.empty()) {
                (void)sys_->drive8.mount(disks_.front());
            }
        }

        // Publish the static description once, post-init.
        spec_.push_back({.label = "System", .value = "Commodore 64"});
        spec_.push_back(
            {.label = "Region", .value = region_ == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = "Media", .value = std::move(display_name)});
        }
        if (disks_.size() > 1U) {
            spec_.push_back({.label = "Disks", .value = std::to_string(disks_.size())});
        }

        // Arm autostart for media that needs a load command typed. Cartridges
        // boot themselves; a bare machine has nothing to start.
        if (autostart && (kind_ == media_kind::disk || kind_ == media_kind::tape)) {
            phase_ = auto_phase::warmup;
        }
    }

    bool c64_adapter::insert_media(std::size_t index) noexcept {
        if (index >= disks_.size()) {
            return false;
        }
        disk_index_ = index;
        return sys_->drive8.mount(disks_[index]);
    }

    void c64_adapter::tick_autostart() {
        switch (phase_) {
        case auto_phase::idle:
        case auto_phase::done:
            return;

        case auto_phase::warmup:
            if (++phase_frames_ < kWarmupFrames) {
                return;
            }
            phase_frames_ = 0U;
            queue_.clear();
            queue_pos_ = 0U;
            hold_frames_ = 0U;
            if (kind_ == media_kind::tape) {
                // SHIFT + RUN/STOP: the KERNAL's tape LOAD-and-RUN shortcut.
                queue_.push_back({.keys = {key::lshift, key::run_stop}, .count = 2U});
                phase_ = auto_phase::type_run;
            } else {
                enqueue_command(queue_, "LOAD\"*\",8,1");
                phase_ = auto_phase::type_load;
            }
            return;

        case auto_phase::type_load:
        case auto_phase::type_run: {
            if (queue_pos_ >= queue_.size()) {
                if (phase_ == auto_phase::type_load) {
                    phase_ = auto_phase::await_drive;
                    phase_frames_ = 0U;
                    drive_was_active_ = false;
                } else {
                    phase_ = auto_phase::done;
                }
                return;
            }
            const key_chord& chord = queue_[queue_pos_];
            const bool down = hold_frames_ < kHoldFrames;
            for (std::uint8_t i = 0; i < chord.count; ++i) {
                sys_->input.set_key(chord.keys[i], down);
            }
            if (++hold_frames_ >= kChordFrames) {
                hold_frames_ = 0U;
                ++queue_pos_;
            }
            return;
        }

        case auto_phase::await_drive:
            // Type RUN once the drive has served the file and gone quiet. If the
            // load never starts (e.g. no real ROMs), time out and stop.
            if (sys_->drive8.transfer_active()) {
                drive_was_active_ = true;
                phase_frames_ = 0U;
            } else if (drive_was_active_ && ++phase_frames_ >= kIdleSettle) {
                queue_.clear();
                queue_pos_ = 0U;
                hold_frames_ = 0U;
                enqueue_command(queue_, "RUN");
                phase_ = auto_phase::type_run;
            } else if (!drive_was_active_ && ++phase_frames_ >= kAwaitTimeout) {
                phase_ = auto_phase::done;
            }
            return;
        }
    }

    frontend_sdk::video_region c64_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    chips::frame_buffer_view c64_adapter::current_frame() const noexcept {
        return sys_->video()->framebuffer();
    }

    void c64_adapter::step_one_frame() {
        tick_autostart();
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

        struct system_roms final {
            std::vector<std::uint8_t> basic;
            std::vector<std::uint8_t> kernal;
            std::vector<std::uint8_t> chargen;
        };

        // Load the BASIC/KERNAL/CHARGEN set from $MNEMOS_C64_ROM_DIR. Any image
        // that can't be found is substituted with a correctly-sized zero buffer
        // so the player still launches without a ROM set (boots a blank machine,
        // exactly as the manifest parity test's synthetic path does).
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
                    auto roms = load_system_roms();
                    const auto region = opts.video_region == mnemos::video_region::ntsc
                                            ? manifests::c64::c64_config::region::ntsc
                                            : manifests::c64::c64_config::region::pal;
                    auto* sched_factory = opts.scheduler_factory_override;
                    return std::make_unique<c64_adapter>(
                        std::move(roms.basic), std::move(roms.kernal), std::move(roms.chargen),
                        std::move(opts.rom), std::move(opts.additional_media), opts.autostart,
                        manifests::c64::c64_config{.video_region = region},
                        std::move(opts.display_name), sched_factory);
                });
            return 0;
        }();

    } // namespace

} // namespace mnemos::apps::player::adapters::c64
