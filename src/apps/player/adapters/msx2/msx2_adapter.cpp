#include "msx2_adapter.hpp"

#include "adapter_registry.hpp"
#include "audio_resampler.hpp"
#include "msx_cartridge_mapper.hpp"
#include "msx_cassette.hpp"
#include "msx_keyboard_matrix.hpp"
#include "msx_kanji_rom.hpp"
#include "wd1793.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace mnemos::apps::player::adapters::msx2 {

    namespace {
        constexpr std::uint32_t k_adapter_state_version = 2U;

        [[nodiscard]] std::string kanji_rom_revision(std::size_t bytes);

        [[nodiscard]] manifests::common::msx_cartridge_mapper_kind
        mapper_kind(manifests::msx2::msx2_cartridge_mapper mapper) noexcept {
            using manifests::common::msx_cartridge_mapper_kind;
            using manifests::msx2::msx2_cartridge_mapper;
            switch (mapper) {
            case msx2_cartridge_mapper::automatic:
                return msx_cartridge_mapper_kind::automatic;
            case msx2_cartridge_mapper::ascii8:
                return msx_cartridge_mapper_kind::ascii8;
            case msx2_cartridge_mapper::ascii8_sram8:
                return msx_cartridge_mapper_kind::ascii8_sram8;
            case msx2_cartridge_mapper::ascii16:
                return msx_cartridge_mapper_kind::ascii16;
            case msx2_cartridge_mapper::ascii16_sram2:
                return msx_cartridge_mapper_kind::ascii16_sram2;
            case msx2_cartridge_mapper::generic8:
                return msx_cartridge_mapper_kind::generic8;
            case msx2_cartridge_mapper::konami:
                return msx_cartridge_mapper_kind::konami;
            case msx2_cartridge_mapper::konami_scc:
                return msx_cartridge_mapper_kind::konami_scc;
            case msx2_cartridge_mapper::korean_msx:
                return msx_cartridge_mapper_kind::korean_msx;
            case msx2_cartridge_mapper::korean_msx_nemesis:
                return msx_cartridge_mapper_kind::korean_msx_nemesis;
            case msx2_cartridge_mapper::plain:
            default:
                return msx_cartridge_mapper_kind::plain;
            }
        }

        [[nodiscard]] manifests::msx2::msx2_cartridge_mapper
        mapper_from_kind(manifests::common::msx_cartridge_mapper_kind mapper) noexcept {
            using manifests::common::msx_cartridge_mapper_kind;
            using manifests::msx2::msx2_cartridge_mapper;
            switch (mapper) {
            case msx_cartridge_mapper_kind::automatic:
                return msx2_cartridge_mapper::automatic;
            case msx_cartridge_mapper_kind::ascii8:
                return msx2_cartridge_mapper::ascii8;
            case msx_cartridge_mapper_kind::ascii8_sram8:
                return msx2_cartridge_mapper::ascii8_sram8;
            case msx_cartridge_mapper_kind::ascii16:
                return msx2_cartridge_mapper::ascii16;
            case msx_cartridge_mapper_kind::ascii16_sram2:
                return msx2_cartridge_mapper::ascii16_sram2;
            case msx_cartridge_mapper_kind::generic8:
                return msx2_cartridge_mapper::generic8;
            case msx_cartridge_mapper_kind::konami:
                return msx2_cartridge_mapper::konami;
            case msx_cartridge_mapper_kind::konami_scc:
                return msx2_cartridge_mapper::konami_scc;
            case msx_cartridge_mapper_kind::korean_msx:
                return msx2_cartridge_mapper::korean_msx;
            case msx_cartridge_mapper_kind::korean_msx_nemesis:
                return msx2_cartridge_mapper::korean_msx_nemesis;
            case msx_cartridge_mapper_kind::plain:
            default:
                return msx2_cartridge_mapper::plain;
            }
        }

        [[nodiscard]] bool has_scc_audio(manifests::msx2::msx2_cartridge_mapper mapper) noexcept {
            return manifests::common::msx_mapper_has_scc_audio(mapper_kind(mapper));
        }

        [[nodiscard]] bool has_scc_audio(const manifests::msx2::msx2_system& sys) noexcept {
            return has_scc_audio(sys.cart_mapper) || has_scc_audio(sys.cart2_mapper);
        }

        std::vector<runtime::scheduled_chip> build_schedule(manifests::msx2::msx2_system& sys) {
            std::vector<runtime::scheduled_chip> chips{
                {&sys.vdp, 1U}, {&sys.cpu, 1U}, {&sys.psg, 1U}, {&sys.cassette, 1U}, {&sys.rtc, 1U},
            };
            if (sys.disk_enabled()) {
                chips.push_back({&sys.fdc, 1U});
            }
            if (has_scc_audio(sys)) {
                chips.push_back({&sys.scc, 1U});
            }
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
                .format = frontend_sdk::input_device_format::digital_pad,
                .device_id = "msx2.joystick.port.1",
                .label = "Joystick 1"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 1U,
                .player_slot = 0U,
                .format = frontend_sdk::input_device_format::keyboard,
                .device_id = "msx2.keyboard.matrix",
                .label = "Keyboard"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 2U,
                .player_slot = 2U,
                .format = frontend_sdk::input_device_format::digital_pad,
                .device_id = "msx2.joystick.port.2",
                .label = "Joystick 2"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 3U,
                .player_slot = 2U,
                .format = frontend_sdk::input_device_format::mouse,
                .device_id = "msx2.mouse.port.2",
                .label = "Mouse Port 2"});
            session.input_ports.push_back(frontend_sdk::session_input_port{
                .port_index = 4U,
                .player_slot = 1U,
                .format = frontend_sdk::input_device_format::mouse,
                .device_id = "msx2.mouse.port.1",
                .label = "Mouse Port 1"});
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        [[nodiscard]] bool is_dsk(std::span<const std::uint8_t> media) noexcept {
            return chips::storage::wd1793::is_supported_dsk(media);
        }

        [[nodiscard]] bool is_cas(std::span<const std::uint8_t> media) noexcept {
            return chips::storage::msx_cassette::has_cas_header(media);
        }

        void append_disk_media(frontend_sdk::media_capability_info& media,
                               std::string_view display_name, std::size_t disk_index,
                               std::uint64_t byte_count, bool disk_write_protected) {
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "disk." + std::to_string(disk_index),
                .label = disk_index == 0U ? (display_name.empty() ? std::string{"Disk"}
                                                                  : std::string{display_name})
                                          : "Disk " + std::to_string(disk_index + 1U),
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = byte_count,
                .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                .provider_id = "msx2.wd1793",
                .revision = disk_write_protected ? "read-only" : "loaded",
                .cache_hint = disk_write_protected ? "resident_read_only" : "resident"});
        }

        frontend_sdk::media_capability_info
        make_media_capabilities(std::string_view display_name, std::uint64_t bios_byte_count,
                                std::uint64_t sub_bios_byte_count,
                                std::uint64_t logo_rom_byte_count,
                                std::uint64_t disk_rom_byte_count,
                                std::uint64_t kanji_rom_byte_count, std::uint64_t cart_byte_count,
                                std::uint64_t cart2_byte_count, std::uint64_t disk_byte_count,
                                std::uint64_t tape_byte_count, bool disk_write_protected,
                                const std::vector<std::vector<std::uint8_t>>& additional_disks) {
            frontend_sdk::media_capability_info media{};
            if (bios_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "bios",
                    .label = "MSX2 BIOS",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = bios_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (sub_bios_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "sub_bios",
                    .label = "MSX2 Sub-ROM",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = sub_bios_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (logo_rom_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "logo_rom",
                    .label = "MSX2 Logo ROM",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = logo_rom_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (disk_rom_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "disk_rom",
                    .label = "Disk Interface ROM",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = disk_rom_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.wd1793",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            if (kanji_rom_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "kanji",
                    .label = "Kanji ROM",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = kanji_rom_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.kanji_rom",
                    .revision = kanji_rom_revision(kanji_rom_byte_count),
                    .cache_hint = "resident"});
            }
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
            if (cart2_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "cart2",
                    .label = "Cartridge Slot 2",
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = cart2_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx2.adapter",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            std::size_t disk_index = 0U;
            if (disk_byte_count != 0U) {
                append_disk_media(media, display_name, disk_index++, disk_byte_count,
                                  disk_write_protected);
            }
            for (const auto& disk : additional_disks) {
                if (is_dsk(disk)) {
                    append_disk_media(media, display_name, disk_index++, disk.size(),
                                      disk_write_protected);
                }
            }
            if (tape_byte_count != 0U) {
                media.media.push_back(frontend_sdk::media_image_info{
                    .id = "tape",
                    .label =
                        display_name.empty() ? std::string{"Cassette"} : std::string{display_name},
                    .residency = frontend_sdk::media_residency::resident,
                    .byte_count = tape_byte_count,
                    .hash_algorithm = frontend_sdk::media_hash_algorithm::none,
                    .provider_id = "msx.cassette",
                    .revision = "loaded",
                    .cache_hint = "resident"});
            }
            return media;
        }

        enum class media_kind : std::uint8_t {
            none,
            cartridge,
            disk,
            tape,
        };

        [[nodiscard]] media_kind classify_media(std::span<const std::uint8_t> media) noexcept {
            if (media.empty()) {
                return media_kind::none;
            }
            if (is_cas(media)) {
                return media_kind::tape;
            }
            if (is_dsk(media)) {
                return media_kind::disk;
            }
            return media_kind::cartridge;
        }

        [[nodiscard]] std::span<const std::uint8_t>
        cartridge_media(std::span<const std::uint8_t> media) noexcept {
            return classify_media(media) == media_kind::cartridge ? media
                                                                  : std::span<const std::uint8_t>{};
        }

        [[nodiscard]] manifests::msx2::msx2_config
        config_for_media(manifests::msx2::msx2_config config, std::span<const std::uint8_t> media,
                         const std::vector<std::vector<std::uint8_t>>& additional_disks) noexcept {
            switch (classify_media(media)) {
            case media_kind::disk:
                config.disk_image = media;
                break;
            case media_kind::tape:
                config.cassette_image = media;
                break;
            case media_kind::none:
            case media_kind::cartridge:
            default:
                break;
            }
            if (config.disk_image.empty()) {
                for (const auto& disk : additional_disks) {
                    if (is_dsk(disk)) {
                        config.disk_image = disk;
                        break;
                    }
                }
            }
            return config;
        }

        [[nodiscard]] std::uint64_t cart_byte_count(std::span<const std::uint8_t> media) noexcept {
            return classify_media(media) == media_kind::cartridge ? media.size() : 0U;
        }

        [[nodiscard]] std::uint64_t
        cart2_byte_count(const manifests::msx2::msx2_config& config) noexcept {
            return classify_media(config.cartridge2) == media_kind::cartridge
                       ? config.cartridge2.size()
                       : 0U;
        }

        [[nodiscard]] std::uint64_t
        disk_byte_count(std::span<const std::uint8_t> media,
                        const manifests::msx2::msx2_config& config) noexcept {
            return classify_media(media) == media_kind::disk ? media.size()
                                                             : config.disk_image.size();
        }

        [[nodiscard]] std::uint64_t
        tape_byte_count(std::span<const std::uint8_t> media,
                        const manifests::msx2::msx2_config& config) noexcept {
            return classify_media(media) == media_kind::tape ? media.size()
                                                             : config.cassette_image.size();
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
            return mapper_from_kind(manifests::common::parse_msx_cartridge_mapper_name(name));
        }

        [[nodiscard]] std::string_view
        mapper_label(manifests::msx2::msx2_cartridge_mapper mapper) noexcept {
            return manifests::common::msx_cartridge_mapper_label(mapper_kind(mapper));
        }

        [[nodiscard]] std::string audio_label(bool scc_audio, bool msx_music) {
            if (scc_audio && msx_music) {
                return "PSG+SCC+MSX-MUSIC";
            }
            if (scc_audio) {
                return "PSG+SCC";
            }
            if (msx_music) {
                return "PSG+MSX-MUSIC";
            }
            return "PSG";
        }

        void apply_machine_profile(manifests::msx2::msx2_config& config,
                                   const frontend_sdk::msx_machine_profile& profile) {
            if (profile.expanded_primary_slots) {
                config.expanded_primary_slots = *profile.expanded_primary_slots;
            }
            if (profile.ram_slot) {
                config.ram_primary_slot = profile.ram_slot->primary;
                config.ram_secondary_slot = profile.ram_slot->secondary;
            }
            if (profile.sub_bios_slot) {
                config.sub_bios_primary_slot = profile.sub_bios_slot->primary;
                config.sub_bios_secondary_slot = profile.sub_bios_slot->secondary;
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
                config.ram_size = *profile.ram_size;
            }
        }

        [[nodiscard]] std::string slot_label(std::uint8_t primary, std::uint8_t secondary) {
            return std::to_string(primary) + "." + std::to_string(secondary);
        }

        [[nodiscard]] std::uint8_t expanded_slot_mask(
            const std::array<bool, 4>& expanded_slot) noexcept {
            std::uint8_t mask = 0U;
            for (std::size_t slot = 0; slot < expanded_slot.size(); ++slot) {
                if (expanded_slot[slot]) {
                    mask = static_cast<std::uint8_t>(mask | (1U << slot));
                }
            }
            return mask;
        }

        [[nodiscard]] std::string slot_layout_label(const manifests::msx2::msx2_system& sys) {
            return "expanded=" + std::to_string(expanded_slot_mask(sys.expanded_slot)) +
                   " ram=" + slot_label(sys.ram_primary_slot, sys.ram_secondary_slot) +
                   " sub=" + slot_label(sys.sub_bios_primary_slot, sys.sub_bios_secondary_slot) +
                   " disk=" + slot_label(sys.disk_primary_slot, sys.disk_secondary_slot) +
                   " cart2=" +
                   slot_label(sys.cartridge2_primary_slot, sys.cartridge2_secondary_slot);
        }

        void apply_mouse_input(manifests::msx2::msx2_system& sys,
                               std::array<msx_mouse_tracker, 2>& mouse_input,
                               std::size_t hardware_port,
                               const frontend_sdk::controller_state& state) noexcept {
            const msx_mouse_delta delta = mouse_input[hardware_port].apply(state);
            sys.set_mouse(static_cast<int>(hardware_port), delta.x, delta.y, delta.left_button,
                          delta.right_button);
        }
    } // namespace

    msx2_adapter::msx2_adapter(std::vector<std::uint8_t> bios, std::vector<std::uint8_t> cart,
                               const manifests::msx2::msx2_config& config, std::string display_name,
                               frontend_sdk::scheduler_factory* scheduler_factory,
                               std::vector<std::vector<std::uint8_t>> additional_disks)
        : session_(make_session_capabilities()),
          media_(make_media_capabilities(
              display_name, bios.size(), config.sub_bios.size(), config.logo_rom.size(),
              config.disk_rom.size(), config.kanji_rom.size(), cart_byte_count(cart),
              cart2_byte_count(config), disk_byte_count(cart, config),
              tape_byte_count(cart, config), config.disk_write_protected, additional_disks)),
          sys_(manifests::msx2::assemble_msx2(bios, cartridge_media(cart),
                                              config_for_media(config, cart, additional_disks))),
          ram_view_("ram_mapper", sys_->ram_view()),
          scheduler_(
              frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->vdp)),
          video_region_(config.video_region),
          target_fps_(mnemos::target_fps[static_cast<std::size_t>(config.video_region)]),
          disk_write_protected_(config.disk_write_protected) {
        const media_kind primary_kind = classify_media(cart);
        const bool primary_is_cartridge = primary_kind == media_kind::cartridge;
        if (primary_kind == media_kind::disk) {
            disks_.push_back(std::move(cart));
        } else if (is_dsk(config.disk_image)) {
            disks_.emplace_back(config.disk_image.begin(), config.disk_image.end());
        }
        for (auto& disk : additional_disks) {
            if (is_dsk(disk)) {
                disks_.push_back(std::move(disk));
            }
        }

        const bool scc_audio = has_scc_audio(*sys_);
        sys_->psg.enable_audio_capture(true);
        if (scc_audio) {
            sys_->scc.enable_audio_capture(true);
        }
        if (sys_->msx_music_enabled()) {
            sys_->music.enable_audio_capture(true);
        }
        chip_view_[chip_count_++] = &sys_->vdp;
        chip_view_[chip_count_++] = &sys_->cpu;
        chip_view_[chip_count_++] = &sys_->psg;
        chip_view_[chip_count_++] = &sys_->cassette;
        chip_view_[chip_count_++] = &sys_->rtc;
        if (sys_->disk_enabled()) {
            chip_view_[chip_count_++] = &sys_->fdc;
        }
        if (!sys_->kanji.empty()) {
            chip_view_[chip_count_++] = &sys_->kanji;
        }
        if (scc_audio) {
            chip_view_[chip_count_++] = &sys_->scc;
        }
        if (sys_->msx_music_enabled()) {
            chip_view_[chip_count_++] = &sys_->music;
        }
        system_mem_view_[0] = &ram_view_;

        spec_.push_back({.label = "System", .value = "MSX2"});
        spec_.push_back(
            {.label = "Region",
             .value = config.video_region == mnemos::video_region::pal ? "PAL" : "NTSC"});
        spec_.push_back({.label = "Slot Layout", .value = slot_layout_label(*sys_)});
        spec_.push_back(
            {.label = "RAM Mapper",
             .value = std::to_string(sys_->ram_view().size() / 1024U) + " KiB"});
        spec_.push_back({.label = "Mapper", .value = std::string{mapper_label(sys_->cart_mapper)}});
        spec_.push_back(
            {.label = "Audio", .value = audio_label(scc_audio, sys_->msx_music_enabled())});
        if (sys_->fdc.mounted()) {
            const auto geometry = sys_->fdc.geometry();
            spec_.push_back({.label = "Disk",
                             .value = std::to_string(geometry.tracks) + "T/" +
                                      std::to_string(geometry.sides) + "S/" +
                                      std::to_string(geometry.sectors_per_track) + "SPT"});
            spec_.push_back({.label = "Disk Mode",
                             .value = sys_->fdc.write_protected() ? "Read-only" : "Writable"});
        }
        if (disks_.size() > 1U) {
            spec_.push_back({.label = "Disks", .value = std::to_string(disks_.size())});
        }
        if (sys_->cassette.loaded()) {
            spec_.push_back({.label = "Cassette", .value = "MSX CAS"});
        }
        if (!sys_->kanji.empty()) {
            spec_.push_back(
                {.label = "Kanji ROM", .value = kanji_rom_label(sys_->kanji.rom_size())});
        }
        if (!sys_->cart_sram.empty()) {
            spec_.push_back({.label = "Cart SRAM",
                             .value = std::to_string(sys_->cart_sram.size() / 1024U) + " KiB"});
        }
        if (!sys_->cartridge2.empty()) {
            spec_.push_back({.label = "Cart Slot 2", .value = "mounted"});
            spec_.push_back({.label = "Cart Slot 2 Mapper",
                             .value = std::string{mapper_label(sys_->cart2_mapper)}});
        }
        if (!sys_->cart2_sram.empty()) {
            spec_.push_back({.label = "Cart Slot 2 SRAM",
                             .value = std::to_string(sys_->cart2_sram.size() / 1024U) + " KiB"});
        }
        if (sys_->fmpac_sram_enabled()) {
            spec_.push_back({.label = "PAC SRAM", .value = "8 KiB"});
        }
        if (!display_name.empty()) {
            spec_.push_back({.label = primary_is_cartridge ? "Cart" : "Media",
                             .value = std::move(display_name)});
        }
    }

    frontend_sdk::video_region msx2_adapter::region() const noexcept {
        return {mnemos::fps_x1000[static_cast<std::size_t>(video_region_)]};
    }

    void msx2_adapter::step_one_frame() { scheduler_.run_frame(); }

    void msx2_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(k_adapter_state_version);
        writer.u64(disks_.size());
        writer.u64(disk_index_);
        for (const msx_mouse_tracker& mouse : mouse_input_) {
            mouse.save_state(writer);
        }
    }

    void msx2_adapter::load_adapter_state(chips::state_reader& reader) {
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

    std::vector<std::uint8_t> msx2_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result msx2_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            psg_buf_.clear();
            scc_buf_.clear();
            music_buf_.clear();
            acc_l_.clear();
            acc_r_.clear();
            mix_buf_.clear();
            audio_frac_ = 0.0;
        }
        return result;
    }

    bool msx2_adapter::insert_media(std::size_t index) noexcept {
        if (index >= disks_.size()) {
            return false;
        }
        if (!sys_->fdc.mount_dsk(disks_[index], disk_write_protected_)) {
            return false;
        }
        disk_index_ = index;
        return true;
    }

    void msx2_adapter::apply_input(int port, const frontend_sdk::controller_state& state) noexcept {
        if (port == 0) {
            sys_->set_joystick(0, state.up, state.down, state.left, state.right, state.a, state.b);
            return;
        }
        if (port == 2) {
            sys_->set_joystick(1, state.up, state.down, state.left, state.right, state.a, state.b);
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
        if (port != 1) {
            return;
        }
        sys_->keyboard_rows = msx_keyboard_matrix_from_input(state);
    }

    frontend_sdk::audio_chunk msx2_adapter::drain_audio() noexcept {
        const bool scc_audio = has_scc_audio(*sys_);
        const std::size_t psg_pairs = sys_->psg.pending_samples();
        const std::size_t scc_pairs = scc_audio ? sys_->scc.pending_samples() : 0U;
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

    runtime::save_target build_save_target(msx2_adapter& adapter) {
        auto* sys = &adapter.system();
        auto* sched = &adapter.scheduler();

        runtime::save_target target;
        target.manifest_id = "msx2";
        target.manifest_rev = 1U;
        target.master_cycle = sched->master_cycle();
        target.chips.push_back({"cpu", &sys->cpu});
        target.chips.push_back({"vdp", &sys->vdp});
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
                    std::span<const std::uint8_t> kanji_rom;
                    if (opts.bios_images.size() > 3U) {
                        kanji_rom = opts.bios_images[3];
                    }
                    std::span<const std::uint8_t> logo_rom;
                    if (opts.bios_images.size() > 4U) {
                        logo_rom = opts.bios_images[4];
                    }
                    std::span<const std::uint8_t> disk_image;
                    std::span<const std::uint8_t> cassette_image;
                    std::span<const std::uint8_t> cartridge2;
                    std::vector<std::vector<std::uint8_t>> additional_disks;
                    for (const auto& media : opts.additional_media) {
                        if (is_dsk(media)) {
                            if (disk_image.empty()) {
                                disk_image = media;
                            } else {
                                additional_disks.push_back(media);
                            }
                        } else if (cassette_image.empty() && is_cas(media)) {
                            cassette_image = media;
                        } else if (cartridge2.empty() &&
                                   classify_media(media) == media_kind::cartridge) {
                            cartridge2 = media;
                        }
                    }
                    manifests::msx2::msx2_config config{
                        .video_region = opts.video_region,
                        .cartridge_mapper = mapper_from_override(opts.mapper_override),
                        .cartridge2_mapper = mapper_from_override(opts.mapper2_override),
                        .msx_music = opts.fm_unit,
                        .fmpac_sram = opts.fm_unit,
                        .cartridge2 = cartridge2,
                        .sub_bios = sub_bios,
                        .logo_rom = logo_rom,
                        .disk_rom = disk_rom,
                        .kanji_rom = kanji_rom,
                        .cassette_image = cassette_image,
                        .disk_image = disk_image};
                    apply_machine_profile(config, opts.msx_profile);
                    return std::make_unique<msx2_adapter>(
                        std::move(bios), std::move(opts.rom), config,
                        std::move(opts.display_name), opts.scheduler_factory_override,
                        std::move(additional_disks));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::msx2
