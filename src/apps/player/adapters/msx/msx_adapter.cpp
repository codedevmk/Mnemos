#include "msx_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"
#include "msx_cartridge_mapper.hpp"
#include "msx_keyboard_matrix.hpp"
#include "msx_kanji_rom.hpp"
#include "wd1793.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::msx {

    namespace {
        constexpr std::uint32_t k_adapter_state_version = 2U;

        [[nodiscard]] std::string kanji_rom_revision(std::size_t bytes);

        using mnemos::dsp::clip_i16;
        using mnemos::dsp::kMixerGainOne;
        using mnemos::dsp::kOutputRate;
        using mnemos::dsp::sample_channel_box;
        using mnemos::dsp::sample_channel_linear;
        using mnemos::dsp::scale_q12;

        [[nodiscard]] manifests::common::msx_cartridge_mapper_kind
        mapper_kind(manifests::msx::msx_cartridge_mapper mapper) noexcept {
            using manifests::common::msx_cartridge_mapper_kind;
            using manifests::msx::msx_cartridge_mapper;
            switch (mapper) {
            case msx_cartridge_mapper::automatic:
                return msx_cartridge_mapper_kind::automatic;
            case msx_cartridge_mapper::ascii8:
                return msx_cartridge_mapper_kind::ascii8;
            case msx_cartridge_mapper::ascii8_sram8:
                return msx_cartridge_mapper_kind::ascii8_sram8;
            case msx_cartridge_mapper::ascii16:
                return msx_cartridge_mapper_kind::ascii16;
            case msx_cartridge_mapper::ascii16_sram2:
                return msx_cartridge_mapper_kind::ascii16_sram2;
            case msx_cartridge_mapper::generic8:
                return msx_cartridge_mapper_kind::generic8;
            case msx_cartridge_mapper::konami:
                return msx_cartridge_mapper_kind::konami;
            case msx_cartridge_mapper::konami_scc:
                return msx_cartridge_mapper_kind::konami_scc;
            case msx_cartridge_mapper::korean_msx:
                return msx_cartridge_mapper_kind::korean_msx;
            case msx_cartridge_mapper::korean_msx_nemesis:
                return msx_cartridge_mapper_kind::korean_msx_nemesis;
            case msx_cartridge_mapper::plain:
            default:
                return msx_cartridge_mapper_kind::plain;
            }
        }

        [[nodiscard]] manifests::msx::msx_cartridge_mapper
        mapper_from_kind(manifests::common::msx_cartridge_mapper_kind mapper) noexcept {
            using manifests::common::msx_cartridge_mapper_kind;
            using manifests::msx::msx_cartridge_mapper;
            switch (mapper) {
            case msx_cartridge_mapper_kind::automatic:
                return msx_cartridge_mapper::automatic;
            case msx_cartridge_mapper_kind::ascii8:
                return msx_cartridge_mapper::ascii8;
            case msx_cartridge_mapper_kind::ascii8_sram8:
                return msx_cartridge_mapper::ascii8_sram8;
            case msx_cartridge_mapper_kind::ascii16:
                return msx_cartridge_mapper::ascii16;
            case msx_cartridge_mapper_kind::ascii16_sram2:
                return msx_cartridge_mapper::ascii16_sram2;
            case msx_cartridge_mapper_kind::generic8:
                return msx_cartridge_mapper::generic8;
            case msx_cartridge_mapper_kind::konami:
                return msx_cartridge_mapper::konami;
            case msx_cartridge_mapper_kind::konami_scc:
                return msx_cartridge_mapper::konami_scc;
            case msx_cartridge_mapper_kind::korean_msx:
                return msx_cartridge_mapper::korean_msx;
            case msx_cartridge_mapper_kind::korean_msx_nemesis:
                return msx_cartridge_mapper::korean_msx_nemesis;
            case msx_cartridge_mapper_kind::plain:
            default:
                return msx_cartridge_mapper::plain;
            }
        }

        [[nodiscard]] bool has_scc_audio(manifests::msx::msx_cartridge_mapper mapper) noexcept {
            return manifests::common::msx_mapper_has_scc_audio(mapper_kind(mapper));
        }

        [[nodiscard]] bool has_scc_audio(const manifests::msx::msx_system& sys) noexcept {
            return has_scc_audio(sys.mapper) || has_scc_audio(sys.cartridge2_mapper);
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

        [[nodiscard]] manifests::msx::msx_config
        config_for_media(manifests::msx::msx_config config, msx_adapter::media_kind kind,
                         bool has_disk_media, bool has_disk_rom,
                         std::span<const std::uint8_t> cassette_image,
                         std::span<const std::uint8_t> logo_rom) noexcept {
            if (kind == msx_adapter::media_kind::disk || has_disk_media || has_disk_rom) {
                config.disk_enabled = true;
                if (config.disk_secondary_slot != 0U) {
                    config.expanded_primary_slots = static_cast<std::uint8_t>(
                        config.expanded_primary_slots | (1U << (config.disk_primary_slot & 0x03U)));
                }
            }
            if (!cassette_image.empty()) {
                config.cassette_image = cassette_image;
            }
            if (!logo_rom.empty()) {
                config.logo_rom = logo_rom;
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

        [[nodiscard]] std::span<const std::uint8_t>
        first_tape_image(std::span<const std::uint8_t> media, msx_adapter::media_kind kind,
                         const std::vector<std::vector<std::uint8_t>>& additional_media) noexcept {
            if (kind == msx_adapter::media_kind::tape) {
                return media;
            }
            for (const auto& image : additional_media) {
                if (classify_media(image) == msx_adapter::media_kind::tape) {
                    return image;
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
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 3U,
                .player_slot = 2U,
                .format = frontend_sdk::input_device_format::mouse,
                .device_id = "msx.mouse.port.2",
                .label = "Mouse Port 2"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 4U,
                .player_slot = 1U,
                .format = frontend_sdk::input_device_format::mouse,
                .device_id = "msx.mouse.port.1",
                .label = "Mouse Port 1"});
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info make_media_capabilities(
            std::string_view display_name, std::uint64_t bios_bytes, std::uint64_t media_bytes,
            msx_adapter::media_kind kind, std::uint64_t tape_bytes, std::uint64_t disk_bytes,
            std::uint64_t cart2_bytes, std::uint64_t kanji_bytes, std::uint64_t logo_bytes,
            bool disk_write_protected,
            const std::vector<std::vector<std::uint8_t>>& additional_disks) {
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
                    .revision = disk_write_protected ? "read-only" : "loaded",
                    .cache_hint = disk_write_protected ? "resident_read_only" : "resident"});
            }
            if (tape_bytes != 0U && kind != msx_adapter::media_kind::tape) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "tape",
                    .label = "Cassette",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = tape_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.cassette",
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
                    .revision = disk_write_protected ? "read-only" : "loaded",
                    .cache_hint = disk_write_protected ? "resident_read_only" : "resident"});
            }
            bool consumed_additional_disk_as_primary =
                kind != msx_adapter::media_kind::disk && disk_bytes != 0U;
            std::size_t disk_index = 1U;
            for (const auto& disk : additional_disks) {
                if (classify_media(disk) != msx_adapter::media_kind::disk) {
                    continue;
                }
                if (consumed_additional_disk_as_primary) {
                    consumed_additional_disk_as_primary = false;
                    continue;
                }
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "disk." + std::to_string(disk_index),
                    .label = "Disk " + std::to_string(disk_index + 1U),
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = disk.size(),
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.fdc",
                    .revision = disk_write_protected ? "read-only" : "loaded",
                    .cache_hint = disk_write_protected ? "resident_read_only" : "resident"});
                ++disk_index;
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
                    .provider_id = "msx.kanji_rom",
                    .revision = kanji_rom_revision(kanji_bytes),
                    .cache_hint = "resident"});
            }
            if (logo_bytes != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "logo_rom",
                    .label = "MSX Logo ROM",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = logo_bytes,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            return media;
        }

        [[nodiscard]] std::string kanji_rom_label(std::size_t bytes) {
            using mnemos::chips::peripheral::msx_kanji_rom;
            const std::size_t levels = msx_kanji_rom::complete_level_count_for_size(bytes);
            if (levels >= 2U) {
                return "JIS level 1+2";
            }
            if (levels == 1U && msx_kanji_rom::has_partial_level_for_size(bytes)) {
                return "JIS level 1 + partial level 2";
            }
            if (levels == 1U) {
                return "JIS level 1";
            }
            return "Partial JIS ROM";
        }

        [[nodiscard]] std::string kanji_rom_revision(std::size_t bytes) {
            using mnemos::chips::peripheral::msx_kanji_rom;
            const std::size_t levels = msx_kanji_rom::complete_level_count_for_size(bytes);
            if (levels >= 2U) {
                return "jis-level-1-2";
            }
            if (levels == 1U && msx_kanji_rom::has_partial_level_for_size(bytes)) {
                return "jis-level-1-partial-level-2";
            }
            if (levels == 1U) {
                return "jis-level-1";
            }
            return "partial";
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
            return mapper_from_kind(manifests::common::parse_msx_cartridge_mapper_name(name));
        }

        [[nodiscard]] std::string_view
        mapper_label(manifests::msx::msx_cartridge_mapper mapper) noexcept {
            return manifests::common::msx_cartridge_mapper_label(mapper_kind(mapper));
        }

        [[nodiscard]] std::size_t ram_mapper_segments_for_size(std::size_t bytes) noexcept {
            constexpr std::size_t k_segment_size = 0x4000U;
            constexpr std::size_t k_min_segments = 4U;
            constexpr std::size_t k_max_segments = 0x100U;
            const std::size_t requested =
                (bytes + k_segment_size - 1U) / k_segment_size;
            return std::min<std::size_t>(std::max(requested, k_min_segments), k_max_segments);
        }

        void apply_machine_profile(manifests::msx::msx_config& config,
                                   const frontend_sdk::msx_machine_profile& profile) {
            if (profile.expanded_primary_slots) {
                config.expanded_primary_slots = *profile.expanded_primary_slots;
            }
            if (profile.ram_slot) {
                config.ram_primary_slot = profile.ram_slot->primary;
                config.ram_secondary_slot = profile.ram_slot->secondary;
            }
            if (profile.disk_slot) {
                config.disk_primary_slot = profile.disk_slot->primary;
                config.disk_secondary_slot = profile.disk_slot->secondary;
            }
            if (profile.cartridge2_slot) {
                config.cartridge2_primary_slot = profile.cartridge2_slot->primary;
                config.cartridge2_secondary_slot = profile.cartridge2_slot->secondary;
            }
            if (profile.ram_size) {
                config.ram_mapper_segments = ram_mapper_segments_for_size(*profile.ram_size);
            }
        }

        [[nodiscard]] std::string slot_label(std::uint8_t primary, std::uint8_t secondary) {
            return std::to_string(primary) + "." + std::to_string(secondary);
        }

        [[nodiscard]] std::string slot_layout_label(const manifests::msx::msx_system& sys) {
            return "expanded=" + std::to_string(sys.expanded_primary_slots) +
                   " ram=" + slot_label(sys.ram_primary_slot, sys.ram_secondary_slot) +
                   " disk=" + slot_label(sys.disk_primary_slot, sys.disk_secondary_slot) +
                   " cart2=" +
                   slot_label(sys.cartridge2_primary_slot, sys.cartridge2_secondary_slot);
        }

        void apply_mouse_input(manifests::msx::msx_system& sys,
                               std::array<msx_mouse_tracker, 2>& mouse_input,
                               std::size_t hardware_port,
                               const frontend_sdk::controller_state& state) noexcept {
            const msx_mouse_delta delta = mouse_input[hardware_port].apply(state);
            sys.set_mouse(static_cast<int>(hardware_port), delta.x, delta.y, delta.left_button,
                          delta.right_button);
        }

    } // namespace

    msx_adapter::msx_adapter(std::vector<std::uint8_t> bios, std::vector<std::uint8_t> cartridge,
                             const manifests::msx::msx_config& config, std::string display_name,
                             frontend_sdk::scheduler_factory* scheduler_factory,
                             std::vector<std::uint8_t> disk_rom,
                             std::vector<std::uint8_t> kanji_rom,
                             std::vector<std::uint8_t> cartridge2,
                             std::vector<std::vector<std::uint8_t>> additional_disks,
                             std::vector<std::uint8_t> logo_rom)
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
                  config_for_media(
                      config, classify_media(cartridge),
                      has_disk_image(classify_media(cartridge), additional_disks), !disk_rom.empty(),
                      first_tape_image(cartridge, classify_media(cartridge), additional_disks),
                      logo_rom))),
          ram_view_("work_ram", sys_->work_ram()),
          scheduler_(frontend_sdk::make_scheduler(
              scheduler_factory,
              build_schedule(*sys_, has_scc_audio(*sys_), sys_->rtc_enabled, sys_->disk_enabled,
                             sys_->fm_music_enabled),
              &sys_->active_video())),
          region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]),
          disk_write_protected_(config.disk_write_protected) {
        media_kind_ = classify_media(cartridge);
        const media_kind cartridge2_kind = classify_media(cartridge2);
        const std::uint64_t media_bytes = cartridge.size();
        const auto tape_image = first_tape_image(cartridge, media_kind_, additional_disks);
        const std::uint64_t tape_bytes =
            !tape_image.empty() ? tape_image.size() : config.cassette_image.size();
        const std::uint64_t disk_bytes =
            first_disk_image(cartridge, media_kind_, additional_disks).size();
        media_ = make_media_capabilities(
            display_name, bios.size(), media_bytes, media_kind_, tape_bytes, disk_bytes,
            cartridge2_kind == media_kind::cartridge ? cartridge2.size() : 0U, kanji_rom.size(),
            logo_rom.size(), disk_write_protected_, additional_disks);

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

        sys_->psg.enable_audio_capture(true);
        chip_view_[chip_count_++] = &sys_->active_video();
        chip_view_[chip_count_++] = &sys_->cpu;
        chip_view_[chip_count_++] = &sys_->psg;
        chip_view_[chip_count_++] = &sys_->cassette;
        if (!sys_->kanji.empty()) {
            chip_view_[chip_count_++] = &sys_->kanji;
        }
        if (sys_->disk_enabled) {
            chip_view_[chip_count_++] = &sys_->fdc;
        }
        const bool scc_audio = has_scc_audio(*sys_);
        if (scc_audio) {
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
        spec_.push_back({.label = "Slot Layout", .value = slot_layout_label(*sys_)});
        if (media_kind_ == media_kind::cartridge) {
            spec_.push_back({.label = "Mapper", .value = std::string{mapper_label(sys_->mapper)}});
        }
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
        if (scc_audio) {
            spec_.push_back({.label = "Cart Audio", .value = "Konami SCC"});
        }
        if (cartridge2_kind == media_kind::cartridge) {
            spec_.push_back({.label = "Cart Slot 2", .value = "mounted"});
            spec_.push_back({.label = "Cart Slot 2 Mapper",
                             .value = std::string{mapper_label(sys_->cartridge2_mapper)}});
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
        if (sys_->cassette.loaded()) {
            spec_.push_back({.label = "Cassette", .value = "MSX CAS"});
        }
        if (media_kind_ == media_kind::disk) {
            spec_.push_back({.label = "Disk", .value = "MSX DSK"});
        }
        if (!disks_.empty() && media_kind_ != media_kind::disk) {
            spec_.push_back({.label = "Disk", .value = "MSX DSK"});
        }
        if (sys_->disk_enabled) {
            spec_.push_back({.label = "Disk Write",
                             .value = sys_->fdc.write_protected() ? "Read-only" : "Writable"});
        }
        if (disks_.size() > 1U) {
            spec_.push_back({.label = "Disks", .value = std::to_string(disks_.size())});
        }
        if (sys_->disk_enabled && !disk_rom.empty()) {
            spec_.push_back({.label = "Disk ROM", .value = "WD1793 interface"});
        }
        if (!sys_->kanji.empty()) {
            spec_.push_back(
                {.label = "Kanji ROM", .value = kanji_rom_label(sys_->kanji.rom_size())});
        }
        if (!sys_->logo_rom.empty()) {
            spec_.push_back({.label = "Logo ROM", .value = "slot 0 $8000-$BFFF"});
        }
    }

    frontend_sdk::video_region msx_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(region_)]};
    }

    void msx_adapter::step_one_frame() { scheduler_.run_frame(); }

    void msx_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(k_adapter_state_version);
        writer.u64(disks_.size());
        writer.u64(disk_index_);
        for (const msx_mouse_tracker& mouse : mouse_input_) {
            mouse.save_state(writer);
        }
    }

    void msx_adapter::load_adapter_state(chips::state_reader& reader) {
        const std::uint32_t version = reader.u32();
        const std::uint64_t disk_count = reader.u64();
        const std::uint64_t disk_index = reader.u64();
        if ((version != 1U && version != k_adapter_state_version) || disk_count != disks_.size() ||
            (disk_count == 0U ? disk_index != 0U : disk_index >= disk_count)) {
            reader.fail();
            return;
        }
        disk_index_ = static_cast<std::size_t>(disk_index);
        if (version >= 2U) {
            for (msx_mouse_tracker& mouse : mouse_input_) {
                mouse.load_state(reader);
            }
        } else {
            for (msx_mouse_tracker& mouse : mouse_input_) {
                mouse.reset();
            }
        }
    }

    std::vector<std::uint8_t> msx_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result msx_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            psg_buf_.clear();
            scc_buf_.clear();
            fm_buf_.clear();
            acc_l_.clear();
            acc_r_.clear();
            mix_buf_.clear();
            audio_frac_ = 0.0;
        }
        return result;
    }

    bool msx_adapter::insert_media(std::size_t index) noexcept {
        if (index >= disks_.size()) {
            return false;
        }
        if (!sys_->fdc.mount_dsk(disks_[index], disk_write_protected_)) {
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
        if (port == 3) {
            apply_mouse_input(*sys_, mouse_input_, 1U, state);
            return;
        }
        if (port == 4) {
            apply_mouse_input(*sys_, mouse_input_, 0U, state);
            return;
        }
        if (port == 1) {
            sys_->keyboard_rows = msx_keyboard_matrix_from_input(state);
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

    runtime::save_target build_save_target(msx_adapter& adapter) {
        auto* sys = &adapter.system();
        auto* sched = &adapter.scheduler();

        runtime::save_target target;
        target.manifest_id = sys->video_model == manifests::msx::msx_video_model::v9938
                                 ? "msx.v9938"
                                 : "msx.tms9918a";
        target.manifest_rev = 1U;
        target.master_cycle = sched->master_cycle();
        target.chips.push_back({"cpu", &sys->cpu});
        target.chips.push_back({"vdp", &sys->active_video()});
        target.chips.push_back({"psg", &sys->psg});
        target.components.push_back(runtime::save_component{
            .id = "system",
            .save = [sys](chips::state_writer& writer) { sys->save_state(writer); },
            .load = [sys](chips::state_reader& reader) { sys->load_state(reader); }});
        target.components.push_back(runtime::save_component{
            .id = "scheduler",
            .save = [sched](chips::state_writer& writer) { sched->save_state(writer); },
            .load = [sched](chips::state_reader& reader) { sched->load_state(reader); }});
        target.components.push_back(runtime::save_component{
            .id = "adapter",
            .save = [&adapter](chips::state_writer& writer) { adapter.save_adapter_state(writer); },
            .load =
                [&adapter](chips::state_reader& reader) { adapter.load_adapter_state(reader); }});
        return target;
    }

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
                    std::span<const std::uint8_t> cassette_image;
                    std::vector<std::vector<std::uint8_t>> disks;
                    for (std::size_t i = 1U; i < opts.additional_media.size(); ++i) {
                        const msx_adapter::media_kind kind =
                            classify_media(opts.additional_media[i]);
                        if (kind == msx_adapter::media_kind::disk) {
                            disks.push_back(std::move(opts.additional_media[i]));
                        } else if (kind == msx_adapter::media_kind::tape &&
                                   cassette_image.empty()) {
                            cassette_image = opts.additional_media[i];
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
                    std::vector<std::uint8_t> logo_rom;
                    if (opts.bios_images.size() > 2U) {
                        logo_rom = std::move(opts.bios_images[2]);
                    }
                    manifests::msx::msx_config config{
                        .video_region = opts.video_region,
                        .video_model = opts.msx2 ? manifests::msx::msx_video_model::v9938
                                                 : manifests::msx::msx_video_model::tms9918a,
                        .cartridge_mapper = mapper_from_override(opts.mapper_override),
                        .cartridge2_mapper = mapper_from_override(opts.mapper2_override),
                        .rtc_enabled = opts.rtc,
                        .fm_music_enabled = opts.fm_unit,
                        .fmpac_sram_enabled = opts.fm_unit,
                        .cassette_image = cassette_image};
                    apply_machine_profile(config, opts.msx_profile);
                    return std::make_unique<msx_adapter>(
                        std::move(opts.rom), std::move(cart), config, std::move(opts.display_name),
                        opts.scheduler_factory_override, std::move(disk_rom),
                        std::move(kanji_rom), std::move(cart2), std::move(disks),
                        std::move(logo_rom));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::msx
