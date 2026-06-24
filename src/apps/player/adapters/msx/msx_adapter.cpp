#include "msx_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"
#include "wd1793.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::msx {

    namespace {
        using mnemos::dsp::clip_i16;
        using mnemos::dsp::kMixerGainOne;
        using mnemos::dsp::kOutputRate;
        using mnemos::dsp::sample_channel_box;
        using mnemos::dsp::sample_channel_linear;
        using mnemos::dsp::scale_q12;

        [[nodiscard]] bool has_scc_audio(manifests::msx::msx_cartridge_mapper mapper) noexcept {
            return mapper == manifests::msx::msx_cartridge_mapper::konami_scc;
        }

        [[nodiscard]] bool has_scc_audio(const manifests::msx::msx_config& config) noexcept {
            return has_scc_audio(config.cartridge_mapper) ||
                   has_scc_audio(config.cartridge2_mapper);
        }

        [[nodiscard]] msx_adapter::media_kind
        classify_media(std::span<const std::uint8_t> media) noexcept {
            if (media.empty()) {
                return msx_adapter::media_kind::none;
            }
            if (chips::storage::msx_cassette::has_cas_header(media)) {
                return msx_adapter::media_kind::tape;
            }
            if (chips::storage::wd1793::is_supported_dsk(media)) {
                return msx_adapter::media_kind::disk;
            }
            return msx_adapter::media_kind::cartridge;
        }

        [[nodiscard]] manifests::msx::msx_config config_for_media(manifests::msx::msx_config config,
                                                                  msx_adapter::media_kind kind,
                                                                  bool has_disk_media,
                                                                  bool has_disk_rom) noexcept {
            if (kind == msx_adapter::media_kind::disk || has_disk_media || has_disk_rom) {
                config.disk_enabled = true;
                if (config.disk_secondary_slot != 0U) {
                    config.expanded_primary_slots = static_cast<std::uint8_t>(
                        config.expanded_primary_slots | (1U << (config.disk_primary_slot & 0x03U)));
                }
            }
            return config;
        }

        [[nodiscard]] std::span<const std::uint8_t>
        first_disk_image(std::span<const std::uint8_t> media, msx_adapter::media_kind kind,
                         const std::vector<std::vector<std::uint8_t>>& additional_disks) noexcept {
            if (kind == msx_adapter::media_kind::disk) {
                return media;
            }
            for (const auto& disk : additional_disks) {
                if (classify_media(disk) == msx_adapter::media_kind::disk) {
                    return disk;
                }
            }
            return {};
        }

        [[nodiscard]] bool
        has_disk_image(msx_adapter::media_kind kind,
                       const std::vector<std::vector<std::uint8_t>>& additional_disks) noexcept {
            return !first_disk_image({}, kind, additional_disks).empty();
        }

        std::vector<runtime::scheduled_chip> build_schedule(manifests::msx::msx_system& sys,
                                                            bool include_scc, bool include_rtc,
                                                            bool include_disk, bool include_fm) {
            std::vector<runtime::scheduled_chip> chips{
                {&sys.active_video(), 1U},
                {&sys.cpu, 1U},
                {&sys.psg, 1U},
                {&sys.cassette, 1U},
            };
            if (include_disk) {
                chips.push_back({&sys.fdc, 1U});
            }
            if (include_scc) {
                chips.push_back({&sys.scc, 1U});
            }
            if (include_fm) {
                chips.push_back({&sys.fm, 1U});
            }
            if (include_rtc) {
                chips.push_back({&sys.rtc, 1U});
            }
            return chips;
        }

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 0U,
                .player_slot = 1U,
                .format = frontend_sdk::input_device_format::digital_pad,
                .device_id = "msx.joystick.port.1",
                .label = "Joystick 1"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 1U,
                .player_slot = 0U,
                .format = frontend_sdk::input_device_format::keyboard,
                .device_id = "msx.keyboard.matrix",
                .label = "Keyboard"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 2U,
                .player_slot = 2U,
                .format = frontend_sdk::input_device_format::digital_pad,
                .device_id = "msx.joystick.port.2",
                .label = "Joystick 2"});
            session.deterministic_frame_input = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info
        make_media_capabilities(std::string_view display_name, std::uint64_t bios_bytes,
                                std::uint64_t media_bytes, msx_adapter::media_kind kind,
                                std::uint64_t disk_bytes, std::uint64_t cart2_bytes,
                                std::uint64_t kanji_bytes) {
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "bios",
                .label = "MSX BIOS",
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = bios_bytes,
                .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                .provider_id = "msx.adapter",
                .revision = "loaded",
                .cache_hint = "resident"});
            if (media_bytes != 0U && kind == msx_adapter::media_kind::cartridge) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "cart",
                    .label =
                        display_name.empty() ? std::string{"Cartridge"} : std::string{display_name},
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = media_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            } else if (media_bytes != 0U && kind == msx_adapter::media_kind::tape) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "tape",
                    .label =
                        display_name.empty() ? std::string{"Cassette"} : std::string{display_name},
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = media_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.cassette",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            } else if (media_bytes != 0U && kind == msx_adapter::media_kind::disk) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "disk",
                    .label = display_name.empty() ? std::string{"Disk"} : std::string{display_name},
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = media_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.fdc",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (disk_bytes != 0U && kind != msx_adapter::media_kind::disk) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "disk",
                    .label = "Disk",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = disk_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.fdc",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (cart2_bytes != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "cart2",
                    .label = "Cartridge Slot 2",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = cart2_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (kanji_bytes != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "kanji",
                    .label = "Kanji ROM",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = kanji_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.kanji",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            return media;
        }

        [[nodiscard]] std::string kanji_rom_label(std::size_t bytes) {
            return bytes >= 0x40000U ? std::string{"JIS1/JIS2"} : std::string{"JIS1"};
        }

        void add_source(std::int32_t* acc_l, std::int32_t* acc_r, const std::int16_t* src,
                        int count, int dst_count) noexcept {
            if (src == nullptr || count <= 0 || dst_count <= 0) {
                return;
            }
            constexpr int chans = 2;
            const double scale = static_cast<double>(count) / static_cast<double>(dst_count);
            for (int i = 0; i < dst_count; ++i) {
                int l = 0;
                int r = 0;
                if (scale > 1.0) {
                    l = sample_channel_box(src, chans, 0, count, scale * i, scale * (i + 1));
                    r = sample_channel_box(src, chans, 1, count, scale * i, scale * (i + 1));
                } else {
                    l = sample_channel_linear(src, chans, 0, count, scale * i);
                    r = sample_channel_linear(src, chans, 1, count, scale * i);
                }
                acc_l[i] += scale_q12(l, kMixerGainOne);
                acc_r[i] += scale_q12(r, kMixerGainOne);
            }
        }

        [[nodiscard]] manifests::msx::msx_cartridge_mapper
        mapper_from_override(const std::string& name) noexcept {
            if (name == "ascii8") {
                return manifests::msx::msx_cartridge_mapper::ascii8;
            }
            if (name == "ascii8-sram8") {
                return manifests::msx::msx_cartridge_mapper::ascii8_sram8;
            }
            if (name == "ascii16") {
                return manifests::msx::msx_cartridge_mapper::ascii16;
            }
            if (name == "ascii16-sram2") {
                return manifests::msx::msx_cartridge_mapper::ascii16_sram2;
            }
            if (name == "konami") {
                return manifests::msx::msx_cartridge_mapper::konami;
            }
            if (name == "konami-scc") {
                return manifests::msx::msx_cartridge_mapper::konami_scc;
            }
            if (name == "korean-msx") {
                return manifests::msx::msx_cartridge_mapper::korean_msx;
            }
            if (name == "korean-msx-nemesis") {
                return manifests::msx::msx_cartridge_mapper::korean_msx_nemesis;
            }
            return manifests::msx::msx_cartridge_mapper::plain;
        }

    } // namespace

    msx_adapter::msx_adapter(std::vector<std::uint8_t> bios, std::vector<std::uint8_t> cartridge,
                             const manifests::msx::msx_config& config, std::string display_name,
                             frontend_sdk::scheduler_factory* scheduler_factory,
                             std::vector<std::uint8_t> disk_rom,
                             std::vector<std::uint8_t> kanji_rom,
                             std::vector<std::uint8_t> cartridge2,
                             std::vector<std::vector<std::uint8_t>> additional_disks)
        : session_(make_session_capabilities()),
          sys_(manifests::msx::assemble_msx(
              bios,
              classify_media(cartridge) == media_kind::cartridge
                  ? std::span<const std::uint8_t>(cartridge)
                  : std::span<const std::uint8_t>{},
              classify_media(cartridge2) == media_kind::cartridge
                  ? std::span<const std::uint8_t>(cartridge2)
                  : std::span<const std::uint8_t>{},
              !disk_rom.empty() ? std::span<const std::uint8_t>(disk_rom)
                                : std::span<const std::uint8_t>{},
              first_disk_image(cartridge, classify_media(cartridge), additional_disks),
              std::span<const std::uint8_t>(kanji_rom),
              config_for_media(config, classify_media(cartridge),
                               has_disk_image(classify_media(cartridge), additional_disks),
                               !disk_rom.empty()))),
          ram_view_("work_ram", sys_->work_ram()),
          scheduler_(frontend_sdk::make_scheduler(
              scheduler_factory,
              build_schedule(*sys_, has_scc_audio(config), sys_->rtc_enabled, sys_->disk_enabled,
                             sys_->fm_music_enabled),
              &sys_->active_video())),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]) {
        media_kind_ = classify_media(cartridge);
        const media_kind cartridge2_kind = classify_media(cartridge2);
        const std::uint64_t media_bytes = cartridge.size();
        if (media_kind_ == media_kind::disk) {
            disks_.push_back(std::move(cartridge));
        }
        for (auto& disk : additional_disks) {
            if (classify_media(disk) == media_kind::disk) {
                disks_.push_back(std::move(disk));
            }
        }
        if (media_kind_ == media_kind::tape && sys_->cassette.load_cas(cartridge)) {
            sys_->cassette.set_play(true);
        }
        media_ = make_media_capabilities(
            display_name, bios.size(), media_bytes, media_kind_,
            disks_.empty() ? 0U : static_cast<std::uint64_t>(disks_.front().size()),
            cartridge2_kind == media_kind::cartridge ? cartridge2.size() : 0U, kanji_rom.size());

        sys_->psg.enable_audio_capture(true);
        chip_view_[chip_count_++] = &sys_->active_video();
        chip_view_[chip_count_++] = &sys_->cpu;
        chip_view_[chip_count_++] = &sys_->psg;
        chip_view_[chip_count_++] = &sys_->cassette;
        if (sys_->disk_enabled) {
            chip_view_[chip_count_++] = &sys_->fdc;
        }
        if (has_scc_audio(config)) {
            sys_->scc.enable_audio_capture(true);
            chip_view_[chip_count_++] = &sys_->scc;
        }
        if (sys_->fm_music_enabled) {
            sys_->fm.enable_audio_capture(true);
            chip_view_[chip_count_++] = &sys_->fm;
        }
        if (config.rtc_enabled) {
            chip_view_[chip_count_++] = &sys_->rtc;
        }
        system_mem_view_[0] = &ram_view_;

        spec_.push_back({.label = "System",
                         .value = config.video_model == manifests::msx::msx_video_model::v9938
                                      ? "MSX2"
                                      : "MSX1"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        if (!display_name.empty()) {
            spec_.push_back({.label = media_kind_ == media_kind::tape   ? "Tape"
                                      : media_kind_ == media_kind::disk ? "Disk"
                                                                        : "Cart",
                             .value = std::move(display_name)});
        }
        if (!sys_->mapped_ram.empty()) {
            spec_.push_back({.label = "RAM Mapper",
                             .value = std::to_string(sys_->mapped_ram.size() / 1024U) + " KiB"});
        }
        if (has_scc_audio(config)) {
            spec_.push_back({.label = "Cart Audio", .value = "Konami SCC"});
        }
        if (cartridge2_kind == media_kind::cartridge) {
            spec_.push_back({.label = "Cart Slot 2", .value = "mounted"});
        }
        if (sys_->fm_music_enabled) {
            spec_.push_back({.label = "Audio", .value = "MSX-MUSIC"});
        }
        if (sys_->fmpac_sram_enabled) {
            spec_.push_back({.label = "PAC SRAM", .value = "8 KiB"});
        }
        if (!sys_->cart_sram.empty()) {
            spec_.push_back({.label = "Cart SRAM",
                             .value = std::to_string(sys_->cart_sram.size() / 1024U) + " KiB"});
        }
        if (!sys_->cart2_sram.empty()) {
            spec_.push_back({.label = "Cart Slot 2 SRAM",
                             .value = std::to_string(sys_->cart2_sram.size() / 1024U) + " KiB"});
        }
        if (config.rtc_enabled) {
            spec_.push_back({.label = "RTC", .value = "RP-5C01"});
        }
        if (media_kind_ == media_kind::tape) {
            spec_.push_back({.label = "Cassette", .value = "MSX CAS"});
        }
        if (media_kind_ == media_kind::disk) {
            spec_.push_back({.label = "Disk", .value = "MSX DSK"});
        }
        if (!disks_.empty() && media_kind_ != media_kind::disk) {
            spec_.push_back({.label = "Disk", .value = "MSX DSK"});
        }
        if (disks_.size() > 1U) {
            spec_.push_back({.label = "Disks", .value = std::to_string(disks_.size())});
        }
        if (sys_->disk_enabled && !disk_rom.empty()) {
            spec_.push_back({.label = "Disk ROM", .value = "WD1793 interface"});
        }
        if (!sys_->kanji_rom.empty()) {
            spec_.push_back(
                {.label = "Kanji ROM", .value = kanji_rom_label(sys_->kanji_rom.size())});
        }
    }

    frontend_sdk::video_region msx_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void msx_adapter::step_one_frame() { scheduler_.run_frame(); }

    bool msx_adapter::insert_media(std::size_t index) noexcept {
        if (index >= disks_.size()) {
            return false;
        }
        if (!sys_->fdc.mount_dsk(disks_[index])) {
            return false;
        }
        disk_index_ = index;
        return true;
    }

    void msx_adapter::apply_input(int port, const frontend_sdk::controller_state& state) noexcept {
        if (port == 0) {
            sys_->apply_joystick(0, state);
            return;
        }
        if (port == 2) {
            sys_->apply_joystick(1, state);
            return;
        }
        if (port == 1) {
            sys_->keyboard_rows.fill(0xFFU);
            sys_->set_key(8, 4, state.left);
            sys_->set_key(8, 7, state.right);
            sys_->set_key(8, 5, state.up);
            sys_->set_key(8, 6, state.down);
            sys_->set_key(8, 0, state.a || state.b); // space
            sys_->set_key(7, 7, state.start);        // return
        }
    }

    frontend_sdk::audio_chunk msx_adapter::drain_audio() noexcept {
        const std::size_t psg_pairs = sys_->psg.pending_samples();
        const std::size_t scc_pairs = sys_->scc.pending_samples();
        const std::size_t fm_pairs = sys_->fm_music_enabled ? sys_->fm.pending_samples() : 0U;
        if (psg_pairs == 0U && scc_pairs == 0U && fm_pairs == 0U) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }
        psg_buf_.resize(psg_pairs * 2U);
        scc_buf_.resize(scc_pairs * 2U);
        fm_buf_.resize(fm_pairs * 2U);
        const int got_psg = static_cast<int>(sys_->psg.drain_samples(psg_buf_.data(), psg_pairs));
        const int got_scc = static_cast<int>(sys_->scc.drain_samples(scc_buf_.data(), scc_pairs));
        const int got_fm = static_cast<int>(sys_->fm.drain_samples(fm_buf_.data(), fm_pairs));
        const int source_pairs = std::max({got_psg, got_scc, got_fm});
        if (source_pairs == 0) {
            return {.samples = nullptr, .frame_count = 0U, .sample_rate = kOutputRate};
        }

        const double exact = (static_cast<double>(kOutputRate) / target_fps_) + audio_frac_;
        int dst_pairs = static_cast<int>(exact);
        if (dst_pairs <= 0) {
            dst_pairs = 1;
        }
        audio_frac_ = exact - static_cast<double>(dst_pairs);

        acc_l_.assign(static_cast<std::size_t>(dst_pairs), 0);
        acc_r_.assign(static_cast<std::size_t>(dst_pairs), 0);
        add_source(acc_l_.data(), acc_r_.data(), psg_buf_.data(), got_psg, dst_pairs);
        add_source(acc_l_.data(), acc_r_.data(), scc_buf_.data(), got_scc, dst_pairs);
        add_source(acc_l_.data(), acc_r_.data(), fm_buf_.data(), got_fm, dst_pairs);

        mix_buf_.resize(static_cast<std::size_t>(dst_pairs) * 2U);
        for (std::size_t i = 0; i < static_cast<std::size_t>(dst_pairs); ++i) {
            mix_buf_[i * 2U] = clip_i16(acc_l_[i]);
            mix_buf_[i * 2U + 1U] = clip_i16(acc_r_[i]);
        }
        return {.samples = mix_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(dst_pairs),
                .sample_rate = kOutputRate};
    }

    void force_link() noexcept {}

    namespace {
        const auto register_msx = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "msx",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    std::vector<std::uint8_t> cart;
                    if (!opts.additional_media.empty()) {
                        cart = std::move(opts.additional_media[0]);
                    }
                    std::vector<std::uint8_t> cart2;
                    std::vector<std::vector<std::uint8_t>> disks;
                    for (std::size_t i = 1U; i < opts.additional_media.size(); ++i) {
                        const msx_adapter::media_kind kind =
                            classify_media(opts.additional_media[i]);
                        if (kind == msx_adapter::media_kind::disk) {
                            disks.push_back(std::move(opts.additional_media[i]));
                        } else if (kind == msx_adapter::media_kind::cartridge && cart2.empty()) {
                            cart2 = std::move(opts.additional_media[i]);
                        }
                    }
                    std::vector<std::uint8_t> disk_rom;
                    if (!opts.bios_images.empty()) {
                        disk_rom = std::move(opts.bios_images.front());
                    }
                    std::vector<std::uint8_t> kanji_rom;
                    if (opts.bios_images.size() > 1U) {
                        kanji_rom = std::move(opts.bios_images[1]);
                    }
                    return std::make_unique<msx_adapter>(
                        std::move(opts.rom), std::move(cart),
                        manifests::msx::msx_config{
                            .video_region = opts.video_region,
                            .video_model = opts.msx2 ? manifests::msx::msx_video_model::v9938
                                                     : manifests::msx::msx_video_model::tms9918a,
                            .cartridge_mapper = mapper_from_override(opts.mapper_override),
                            .cartridge2_mapper = mapper_from_override(opts.mapper2_override),
                            .rtc_enabled = opts.rtc,
                            .fm_music_enabled = opts.fm_unit,
                            .fmpac_sram_enabled = opts.fm_unit},
                        std::move(opts.display_name), opts.scheduler_factory_override,
                        std::move(disk_rom), std::move(kanji_rom), std::move(cart2),
                        std::move(disks));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::msx
