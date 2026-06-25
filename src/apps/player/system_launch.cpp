#include "system_launch.hpp"

#include "adapter_link.hpp"
#include "adapter_registry.hpp"
#include "genesis_region.hpp"
#include "rom_loader.hpp"
#include "sms_region.hpp"
#include "system_family.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

#if defined(_MSC_VER)
#define MNEMOS_PLAYER_GETENV(name_) mnemos_player_getenv_msvc(name_)
    [[nodiscard]] const char* mnemos_player_getenv_msvc(const char* name) {
#pragma warning(push)
#pragma warning(disable : 4996)
        const char* value = std::getenv(name);
#pragma warning(pop)
        return value;
    }
#else
#define MNEMOS_PLAYER_GETENV(name_) std::getenv(name_)
#endif

    [[nodiscard]] std::string lowercase_extension(const std::string& path) {
        std::string ext = path.size() >= 4U ? path.substr(path.size() - 4U) : std::string{};
        for (char& ch : ext) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return ext;
    }

    void log_media_validation_issues(const mnemos::frontend_sdk::player_system& system) {
        const auto& capabilities = system.media_capabilities();
        bool any = false;
        for (const auto& image : capabilities.media) {
            const char* id = image.id.empty() ? "<unnamed>" : image.id.c_str();
            for (const auto& issue : image.validation_issues) {
                const char* code = issue.code.empty() ? "media.validation" : issue.code.c_str();
                if (issue.detail.empty()) {
                    std::fprintf(stderr, "[mnemos_player] media validation issue: %s: %s\n", id,
                                 code);
                } else {
                    std::fprintf(stderr, "[mnemos_player] media validation issue: %s: %s: %s\n", id,
                                 code, issue.detail.c_str());
                }
                any = true;
            }
        }
        if (any) {
            std::fflush(stderr);
        }
    }

} // namespace

namespace mnemos::apps::player {

    system_launch_outcome launch_system(const system_launch_options& options) {
        using adapters::clean_rom_name;
        using adapters::family_from_name;
        using adapters::family_id;
        using adapters::family_label;
        using adapters::family_names;
        using adapters::load_rom;
        using adapters::load_rom_verbatim;
        using adapters::resolve_video_region;
        using adapters::system_family;

        system_launch_outcome outcome{};
        if (options.rom_paths.empty()) {
            return outcome;
        }

        if (!options.system_arg) {
            std::fprintf(stderr,
                         "[mnemos_player] --system <name> is required with --rom "
                         "(one of: %s)\n",
                         family_names());
            outcome.exit_code = 1;
            return outcome;
        }

        const auto family_opt = family_from_name(*options.system_arg);
        if (!family_opt) {
            std::fprintf(stderr, "[mnemos_player] unknown system '%s' (one of: %s)\n",
                         options.system_arg->c_str(), family_names());
            outcome.exit_code = 1;
            return outcome;
        }

        const system_family family = *family_opt;
        const bool arcade_family =
            family == system_family::irem_m72 || family == system_family::irem_m81 ||
            family == system_family::irem_m82 || family == system_family::irem_m84 ||
            family == system_family::irem_m107 || family == system_family::taito_f2 ||
            family == system_family::capcom_cps1 || family == system_family::capcom_cps2;
        auto loaded = arcade_family ? load_rom_verbatim(options.rom_paths.front())
                                    : load_rom(options.rom_paths.front());
        const bool directory_backed_irem =
            loaded && loaded->directory_source &&
            (family == system_family::irem_m72 || family == system_family::irem_m81 ||
             family == system_family::irem_m82 || family == system_family::irem_m84 ||
             family == system_family::irem_m107);
        if (!loaded || (loaded->bytes.empty() && !directory_backed_irem)) {
            std::fprintf(stderr, "could not read ROM: %s\n", options.rom_paths.front().c_str());
            outcome.exit_code = 1;
            return outcome;
        }

        std::vector<std::vector<std::uint8_t>> additional_media;
        std::vector<std::string> additional_media_paths;
        for (std::size_t i = 1; i < options.rom_paths.size(); ++i) {
            const bool irem_family =
                family == system_family::irem_m72 || family == system_family::irem_m81 ||
                family == system_family::irem_m82 || family == system_family::irem_m84 ||
                family == system_family::irem_m107;
            auto extra = irem_family ? load_rom_verbatim(options.rom_paths[i])
                                     : load_rom(options.rom_paths[i]);
            const bool directory_backed_extra = extra && extra->directory_source && irem_family;
            if (!extra || (extra->bytes.empty() && !directory_backed_extra)) {
                std::fprintf(stderr, "could not read media: %s\n", options.rom_paths[i].c_str());
                outcome.exit_code = 1;
                return outcome;
            }
            additional_media.push_back(std::move(extra->bytes));
            additional_media_paths.push_back(options.rom_paths[i]);
        }

        mnemos::video_region cart_default = mnemos::video_region::ntsc;
        switch (family) {
        case system_family::sms:
            cart_default =
                mnemos::default_video_for(mnemos::manifests::sms::parse_market(loaded->bytes));
            break;
        case system_family::gg:
            cart_default = mnemos::video_region::ntsc;
            break;
        case system_family::genesis:
            cart_default =
                mnemos::default_video_for(mnemos::manifests::genesis::parse_market(loaded->bytes));
            break;
        case system_family::c64:
            cart_default = mnemos::video_region::pal;
            break;
        case system_family::spectrum:
            cart_default = mnemos::video_region::pal;
            break;
        case system_family::nes:
            cart_default = mnemos::video_region::ntsc;
            break;
        case system_family::msx:
            cart_default = mnemos::video_region::ntsc;
            break;
        case system_family::amiga500:
            cart_default = mnemos::video_region::pal;
            break;
        case system_family::msx2:
            cart_default = mnemos::video_region::ntsc;
            break;
        case system_family::sega32x:
            cart_default =
                mnemos::default_video_for(mnemos::manifests::genesis::parse_market(loaded->bytes));
            break;
        case system_family::segacd:
        case system_family::irem_m72:
        case system_family::irem_m81:
        case system_family::irem_m82:
        case system_family::irem_m84:
        case system_family::irem_m107:
        case system_family::taito_f2:
        case system_family::capcom_cps1:
        case system_family::capcom_cps2:
            break;
        }

        const auto video = resolve_video_region(options.region_override, cart_default);
        std::fprintf(stderr, "[mnemos_player] system: %s  region: %s (%s)\n", family_label(family),
                     video == mnemos::video_region::pal ? "PAL" : "NTSC",
                     adapters::region_source_label(options.region_override));
        std::fflush(stderr);

        force_link_all_adapters();

        std::vector<std::uint8_t> primary_rom = std::move(loaded->bytes);
        std::string disc_path;
        if (family == system_family::segacd) {
            const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_SEGACD_BIOS");
            if (bios_env == nullptr) {
                std::fprintf(
                    stderr, "[mnemos_player] Sega CD needs MNEMOS_SEGACD_BIOS set to a BIOS ROM\n");
                outcome.exit_code = 1;
                return outcome;
            }
            auto bios = load_rom(bios_env);
            if (!bios || bios->bytes.empty()) {
                std::fprintf(stderr, "[mnemos_player] could not read Sega CD BIOS: %s\n", bios_env);
                outcome.exit_code = 1;
                return outcome;
            }
            disc_path = options.rom_paths.front();
            primary_rom = std::move(bios->bytes);
        }

        std::vector<std::vector<std::uint8_t>> bios_images;
        if (family == system_family::sega32x) {
            const char* bios_dir = MNEMOS_PLAYER_GETENV("MNEMOS_32X_BIOS_DIR");
            if (bios_dir == nullptr) {
                std::fprintf(stderr, "[mnemos_player] MNEMOS_32X_BIOS_DIR unset; booting without "
                                     "the 32X boot ROMs (most carts will not start)\n");
            } else {
                const std::string dir{bios_dir};
                for (const char* name : {"32X_M_BIOS.bin", "32X_S_BIOS.bin", "32X_G_BIOS.bin"}) {
                    auto image = load_rom(dir + "/" + name);
                    if (!image || image->bytes.empty()) {
                        std::fprintf(stderr, "[mnemos_player] could not read 32X boot ROM: %s/%s\n",
                                     dir.c_str(), name);
                        outcome.exit_code = 1;
                        return outcome;
                    }
                    bios_images.push_back(std::move(image->bytes));
                }
            }
        }

        if (family == system_family::spectrum) {
            const std::string ext = lowercase_extension(options.rom_paths.front());
            if (ext == ".z80" || ext == ".sna") {
                const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_SPECTRUM_ROM");
                if (bios_env == nullptr) {
                    std::fprintf(stderr, "[mnemos_player] a Spectrum snapshot needs "
                                         "MNEMOS_SPECTRUM_ROM set to a 48K system ROM\n");
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < 0x4000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read Spectrum ROM: %s\n",
                                 bios_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom = std::move(bios->bytes);
            }
        }

        if (family == system_family::nes) {
            const std::string ext = lowercase_extension(options.rom_paths.front());
            if (ext == ".fds") {
                const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_FDS_BIOS");
                if (bios_env == nullptr) {
                    std::fprintf(stderr, "[mnemos_player] a Famicom Disk System disk needs "
                                         "MNEMOS_FDS_BIOS set to the 8 KiB DISKSYS.ROM\n");
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < 0x2000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read FDS BIOS: %s\n", bios_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                bios_images.push_back(std::move(bios->bytes));
            }
        }

        if (family == system_family::msx) {
            const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX_BIOS");
            const bool primary_is_dsk = lowercase_extension(options.rom_paths.front()) == ".dsk";
            if (bios_env != nullptr && bios_env[0] != '\0') {
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < 0x8000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX BIOS: %s\n", bios_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom = std::move(bios->bytes);
            } else if (primary_is_dsk) {
                std::fprintf(stderr, "[mnemos_player] an MSX disk needs MNEMOS_MSX_BIOS set to "
                                     "a system BIOS ROM\n");
                outcome.exit_code = 1;
                return outcome;
            } else {
                std::fprintf(stderr, "[mnemos_player] MNEMOS_MSX_BIOS unset; treating --rom as "
                                     "the MSX BIOS image (no cartridge mounted)\n");
            }
            bool disk_requested = primary_is_dsk;
            for (const auto& path : options.rom_paths) {
                disk_requested = disk_requested || lowercase_extension(path) == ".dsk";
            }
            if (disk_requested) {
                const char* disk_rom_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX_DISK_ROM");
                if (disk_rom_env == nullptr || disk_rom_env[0] == '\0') {
                    std::fprintf(stderr, "[mnemos_player] an MSX disk needs "
                                         "MNEMOS_MSX_DISK_ROM set to a disk interface ROM\n");
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto disk_rom = load_rom(disk_rom_env);
                if (!disk_rom || disk_rom->bytes.size() < 0x4000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX disk ROM: %s\n",
                                 disk_rom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                bios_images.push_back(std::move(disk_rom->bytes));
            }
            const char* kanji_rom_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX_KANJI_ROM");
            if (kanji_rom_env != nullptr && kanji_rom_env[0] != '\0') {
                auto kanji_rom = load_rom(kanji_rom_env);
                if (!kanji_rom || kanji_rom->bytes.size() < 0x20000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX Kanji ROM: %s\n",
                                 kanji_rom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                if (!disk_requested) {
                    bios_images.emplace_back();
                }
                bios_images.push_back(std::move(kanji_rom->bytes));
            }
        }

        if (family == system_family::amiga500) {
            const std::string ext = lowercase_extension(options.rom_paths.front());
            if (ext == ".adf") {
                const char* kickstart_env = MNEMOS_PLAYER_GETENV("MNEMOS_AMIGA500_KICKSTART");
                if (kickstart_env == nullptr) {
                    std::fprintf(stderr, "[mnemos_player] an Amiga ADF needs "
                                         "MNEMOS_AMIGA500_KICKSTART set to a Kickstart ROM\n");
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto kickstart = load_rom(kickstart_env);
                if (!kickstart || kickstart->bytes.empty()) {
                    std::fprintf(stderr, "[mnemos_player] could not read Amiga Kickstart ROM: %s\n",
                                 kickstart_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom = std::move(kickstart->bytes);
            }
        }

        if (family == system_family::msx2) {
            const std::string ext = lowercase_extension(options.rom_paths.front());
            if (ext == ".dsk") {
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom.clear();
            }

            const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX2_BIOS");
            if (bios_env == nullptr) {
                std::fprintf(stderr, "[mnemos_player] MSX2 needs MNEMOS_MSX2_BIOS set to a "
                                     "main BIOS ROM\n");
                outcome.exit_code = 1;
                return outcome;
            }
            auto bios = load_rom(bios_env);
            if (!bios || bios->bytes.size() < 0x8000U) {
                std::fprintf(stderr, "[mnemos_player] could not read MSX2 BIOS: %s\n", bios_env);
                outcome.exit_code = 1;
                return outcome;
            }
            bios_images.push_back(std::move(bios->bytes));

            if (const char* subrom_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX2_SUBROM");
                subrom_env != nullptr) {
                auto subrom = load_rom(subrom_env);
                if (!subrom || subrom->bytes.size() < 0x4000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 sub-ROM: %s\n",
                                 subrom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                bios_images.push_back(std::move(subrom->bytes));
            }

            if (const char* diskrom_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX2_DISKROM");
                diskrom_env != nullptr) {
                auto diskrom = load_rom(diskrom_env);
                if (!diskrom || diskrom->bytes.size() < 0x4000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 disk ROM: %s\n",
                                 diskrom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                while (bios_images.size() < 2U) {
                    bios_images.push_back({});
                }
                bios_images.push_back(std::move(diskrom->bytes));
            }
        }

        outcome.system = frontend_sdk::adapter_registry::instance().create(
            family_id(family),
            {.rom = std::move(primary_rom),
             .video_region = video,
             .display_name = clean_rom_name(loaded->name),
             .additional_media = std::move(additional_media),
             .additional_media_paths = std::move(additional_media_paths),
             .autostart = options.autostart,
             .dip_override = options.dip_override,
             .mapper_override = options.mapper_override.value_or(std::string{}),
             .mapper2_override = options.mapper2_override.value_or(std::string{}),
             .fm_unit = options.fm_unit,
             .light_gun = options.light_gun,
             .four_score = options.four_score,
             .rtc = options.rtc,
             .msx2 = options.msx2,
             .disc_path = std::move(disc_path),
             .rom_path = options.rom_paths.front(),
             .bios_images = std::move(bios_images)});
        if (!outcome.system) {
            std::fprintf(stderr, "[mnemos_player] no adapter registered for family '%s'\n",
                         family_id(family));
            outcome.exit_code = 1;
            return outcome;
        }
        log_media_validation_issues(*outcome.system);
        if (outcome.system->media_count() > 1U) {
            std::fprintf(stderr, "[mnemos_player] media set: %zu disks (F6 swaps)\n",
                         outcome.system->media_count());
        }

        outcome.primary_media_path = options.rom_paths.front();
        return outcome;
    }

} // namespace mnemos::apps::player

#if defined(MNEMOS_PLAYER_GETENV)
#undef MNEMOS_PLAYER_GETENV
#endif
