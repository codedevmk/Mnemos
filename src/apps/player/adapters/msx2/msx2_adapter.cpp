#include "msx2_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::msx2 {

    namespace {
        std::vector<runtime::scheduled_chip> build_schedule(manifests::msx2::msx2_system& sys) {
            std::vector<runtime::scheduled_chip> chips{
                {&sys.vdp, 1U}, {&sys.cpu, 1U}, {&sys.psg, 1U},
                {&sys.rtc, 1U}, {&sys.fdc, 1U}, {&sys.scc, 1U},
            };
            if (sys.msx_music_enabled()) {
                chips.push_back({&sys.music, 1U});
            }
            return chips;
        }

        constexpr int kGainPsg = mnemos::dsp::kMixerGainOne / 3;
        constexpr int kGainScc = mnemos::dsp::kMixerGainOne / 3;
        constexpr int kGainMusic = mnemos::dsp::kMixerGainOne / 3;

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 0U,
                .player_slot = 1U,
                .format = frontend_sdk::input_device_format::keyboard,
                .device_id = "msx2.keyboard",
                .label = "Keyboard"});
            session.deterministic_frame_input = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    std::uint64_t cart_byte_count,
                                                                    std::uint64_t disk_byte_count,
                                                                    bool disk_write_protected) {
            frontend_sdk::media_capability_info media{};
            if (cart_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "cart",
                    .label =
                        display_name.empty() ? std::string{"Cartridge"} : std::string{display_name},
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = cart_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (disk_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "disk.0",
                    .label = display_name.empty() ? std::string{"Disk"} : std::string{display_name},
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = disk_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.wd1793",
                    .revision = disk_write_protected ? "read-only" : "loaded",
                    .cache_hint = disk_write_protected ? "resident_read_only" : "resident"});
            }
            return media;
        }

        void add_source(std::int32_t* acc_l, std::int32_t* acc_r, const std::int16_t* src,
                        int count, int gain, int dst_count) noexcept {
            if (src == nullptr || count <= 0 || dst_count <= 0) {
                return;
            }
            const double scale = static_cast<double>(count) / static_cast<double>(dst_count);
            for (int i = 0; i < dst_count; ++i) {
                int l = 0;
                int r = 0;
                if (scale > 1.0) {
                    l = mnemos::dsp::sample_channel_box(src, 2, 0, count, scale * i,
                                                        scale * (i + 1));
                    r = mnemos::dsp::sample_channel_box(src, 2, 1, count, scale * i,
                                                        scale * (i + 1));
                } else {
                    l = mnemos::dsp::sample_channel_linear(src, 2, 0, count, scale * i);
                    r = mnemos::dsp::sample_channel_linear(src, 2, 1, count, scale * i);
                }
                acc_l[i] += mnemos::dsp::scale_q12(l, gain);
                acc_r[i] += mnemos::dsp::scale_q12(r, gain);
            }
        }

        [[nodiscard]] manifests::msx2::msx2_cartridge_mapper
        mapper_from_override(const std::string& name) noexcept {
            if (name == "plain" || name.empty()) {
                return manifests::msx2::msx2_cartridge_mapper::plain;
            }
            if (name == "ascii8") {
                return manifests::msx2::msx2_cartridge_mapper::ascii8;
            }
            if (name == "ascii16") {
                return manifests::msx2::msx2_cartridge_mapper::ascii16;
            }
            if (name == "konami" || name == "konami4") {
                return manifests::msx2::msx2_cartridge_mapper::konami;
            }
            if (name == "konamiscc" || name == "konami-scc" || name == "konami_scc" ||
                name == "scc") {
                return manifests::msx2::msx2_cartridge_mapper::konami_scc;
            }
            return manifests::msx2::msx2_cartridge_mapper::plain;
        }

        [[nodiscard]] std::string_view
        mapper_label(manifests::msx2::msx2_cartridge_mapper mapper) noexcept {
            using manifests::msx2::msx2_cartridge_mapper;
            switch (mapper) {
            case msx2_cartridge_mapper::ascii8:
                return "ASCII8";
            case msx2_cartridge_mapper::ascii16:
                return "ASCII16";
            case msx2_cartridge_mapper::konami:
                return "Konami";
            case msx2_cartridge_mapper::konami_scc:
                return "Konami SCC";
            case msx2_cartridge_mapper::plain:
            default:
                return "Plain";
            }
        }
    } // namespace

    msx2_adapter::msx2_adapter(std::vector<std::uint8_t> bios, std::vector<std::uint8_t> cart,
                               const manifests::msx2::msx2_config& config, std::string display_name,
                               frontend_sdk::scheduler_factory* scheduler_factory)
        : session_(make_session_capabilities()),
          media_(make_media_capabilities(display_name, cart.size(), config.disk_image.size(),
                                         config.disk_write_protected)),
          sys_(manifests::msx2::assemble_msx2(bios, cart, config)),
          ram_view_("ram_mapper", sys_->ram_view()),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->vdp)),
          video_region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        sys_->psg.enable_audio_capture(true);
        sys_->scc.enable_audio_capture(true);
        if (sys_->msx_music_enabled()) {
            sys_->music.enable_audio_capture(true);
        }
        chip_view_[chip_count_++] = &sys_->vdp;
        chip_view_[chip_count_++] = &sys_->cpu;
        chip_view_[chip_count_++] = &sys_->psg;
        chip_view_[chip_count_++] = &sys_->rtc;
        chip_view_[chip_count_++] = &sys_->fdc;
        chip_view_[chip_count_++] = &sys_->scc;
        if (sys_->msx_music_enabled()) {
            chip_view_[chip_count_++] = &sys_->music;
        }
        system_mem_view_[0] = &ram_view_;

        spec_.push_back({.label = "System", .value = "MSX2"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        spec_.push_back(
            {.label = "Mapper", .value = std::string{mapper_label(config.cartridge_mapper)}});
        spec_.push_back({.label = "Audio",
                         .value = sys_->msx_music_enabled() ? "PSG+SCC+MSX-MUSIC" : "PSG+SCC"});
        if (sys_->fdc.mounted()) {
            const auto geometry = sys_->fdc.geometry();
            spec_.push_back({.label = "Disk",
                             .value = std::to_string(geometry.tracks) + "T/" +
                                      std::to_string(geometry.sides) + "S/" +
                                      std::to_string(geometry.sectors_per_track) + "SPT"});
            spec_.push_back({.label = "Disk Mode",
                             .value = sys_->fdc.write_protected() ? "Read-only" : "Writable"});
        }
        if (!display_name.empty()) {
            spec_.push_back(
                {.label = cart.empty() ? "Media" : "Cart", .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region msx2_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(video_region_)]};
    }

    void msx2_adapter::step_one_frame() { scheduler_.run_frame(); }

    void msx2_adapter::apply_input(int port, const frontend_sdk::controller_state& state) noexcept {
        if (port != 0) {
            return;
        }
        sys_->keyboard_rows.fill(0xFFU);
        const bool fire = state.a || state.b;
        sys_->set_joystick(0, state.up, state.down, state.left, state.right, state.a, state.b);
        sys_->set_key(8, 4, state.left);
        sys_->set_key(8, 5, state.up);
        sys_->set_key(8, 6, state.down);
        sys_->set_key(8, 7, state.right);
        sys_->set_key(8, 0, fire);        // SPACE
        sys_->set_key(7, 7, state.start); // RETURN
        sys_->set_key(6, 0, state.select);
    }

    frontend_sdk::audio_chunk msx2_adapter::drain_audio() noexcept {
        const std::size_t psg_pairs = sys_->psg.pending_samples();
        const std::size_t scc_pairs = sys_->scc.pending_samples();
        const std::size_t music_pairs =
            sys_->msx_music_enabled() ? sys_->music.pending_samples() : 0U;
        if (psg_pairs == 0U && scc_pairs == 0U && music_pairs == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
        }

        int psg_got = 0;
        if (psg_pairs > 0U) {
            psg_buf_.resize(psg_pairs * 2U);
            psg_got = static_cast<int>(sys_->psg.drain_samples(psg_buf_.data(), psg_pairs));
        } else {
            psg_buf_.clear();
        }

        int scc_got = 0;
        if (scc_pairs > 0U) {
            scc_buf_.resize(scc_pairs * 2U);
            scc_got = static_cast<int>(sys_->scc.drain_samples(scc_buf_.data(), scc_pairs));
        } else {
            scc_buf_.clear();
        }

        int music_got = 0;
        if (music_pairs > 0U) {
            music_buf_.resize(music_pairs * 2U);
            music_got = static_cast<int>(sys_->music.drain_samples(music_buf_.data(), music_pairs));
        } else {
            music_buf_.clear();
        }

        if (psg_got <= 0 && scc_got <= 0 && music_got <= 0) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = mnemos::dsp::kOutputRate};
        }

        const double exact =
            (static_cast<double>(mnemos::dsp::kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        acc_l_.assign(static_cast<std::size_t>(dst_pairs), 0);
        acc_r_.assign(static_cast<std::size_t>(dst_pairs), 0);
        add_source(acc_l_.data(), acc_r_.data(), psg_buf_.data(), psg_got, kGainPsg, dst_pairs);
        add_source(acc_l_.data(), acc_r_.data(), scc_buf_.data(), scc_got, kGainScc, dst_pairs);
        add_source(acc_l_.data(), acc_r_.data(), music_buf_.data(), music_got, kGainMusic,
                   dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(dst_pairs); ++i) {
            mix_buf_[i * 2U] = mnemos::dsp::clip_i16(acc_l_[i]);
            mix_buf_[i * 2U + 1U] = mnemos::dsp::clip_i16(acc_r_[i]);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = mnemos::dsp::kOutputRate};
    }

    void force_link() noexcept {}

    namespace {
        const auto register_msx2 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "msx2",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    std::vector<std::uint8_t> bios;
                    if (!opts.bios_images.empty()) {
                        bios = std::move(opts.bios_images.front());
                    }
                    std::span<const std::uint8_t> sub_bios;
                    if (opts.bios_images.size() > 1U) {
                        sub_bios = opts.bios_images[1];
                    }
                    std::span<const std::uint8_t> disk_rom;
                    if (opts.bios_images.size() > 2U) {
                        disk_rom = opts.bios_images[2];
                    }
                    std::span<const std::uint8_t> disk_image;
                    if (!opts.additional_media.empty()) {
                        disk_image = opts.additional_media.front();
                    }
                    return std::make_unique<msx2_adapter>(
                        std::move(bios), std::move(opts.rom),
                        manifests::msx2::msx2_config{.video_region = opts.video_region,
                                                     .cartridge_mapper =
                                                         mapper_from_override(opts.mapper_override),
                                                     .msx_music = opts.fm_unit,
                                                     .sub_bios = sub_bios,
                                                     .disk_rom = disk_rom,
                                                     .disk_image = disk_image},
                        std::move(opts.display_name), opts.scheduler_factory_override);
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::msx2
