#include "system_launch.hpp"

#include "adapter_link.hpp"
#include "adapter_registry.hpp"
#include "amiga_system.hpp"
#include "genesis_region.hpp"
#include "msx_cassette.hpp"
#include "rom_loader.hpp"
#include "sms_region.hpp"
#include "system_family.hpp"
#include "wd1793.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    [[nodiscard]] const char* getenv_nonempty(const char* name) noexcept {
        const char* value = MNEMOS_PLAYER_GETENV(name);
        return value != nullptr && value[0] != '\0' ? value : nullptr;
    }

    [[nodiscard]] const char* getenv_nonempty(const char* first, const char* second) noexcept {
        if (const char* value = getenv_nonempty(first); value != nullptr) {
            return value;
        }
        return getenv_nonempty(second);
    }

    [[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1U);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1U);
        }
        return value;
    }

    [[nodiscard]] bool parse_unsigned(std::string_view value, unsigned long long maximum,
                                      unsigned long long& parsed) {
        value = trim_ascii(value);
        if (value.empty() || value.front() == '-') {
            return false;
        }

        const std::string text(value);
        char* end = nullptr;
        const unsigned long long candidate = std::strtoull(text.c_str(), &end, 0);
        if (end == text.c_str() || end == nullptr || *end != '\0' || candidate > maximum) {
            return false;
        }

        parsed = candidate;
        return true;
    }

    [[nodiscard]] std::optional<mnemos::frontend_sdk::msx_slot_location>
    parse_msx_slot_location_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        const std::size_t separator = value.find_first_of(".:");
        unsigned long long primary = 0U;
        unsigned long long secondary = 0U;
        if (separator == std::string_view::npos) {
            if (!parse_unsigned(value, 3U, primary)) {
                return std::nullopt;
            }
        } else if (!parse_unsigned(value.substr(0U, separator), 3U, primary) ||
                   !parse_unsigned(value.substr(separator + 1U), 3U, secondary)) {
            return std::nullopt;
        }

        return mnemos::frontend_sdk::msx_slot_location{static_cast<std::uint8_t>(primary),
                                                       static_cast<std::uint8_t>(secondary)};
    }

    [[nodiscard]] std::optional<std::uint8_t>
    parse_msx_expanded_slots_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        if (value.find_first_of(",;") == std::string_view::npos) {
            unsigned long long mask = 0U;
            if (!parse_unsigned(value, 0x0FU, mask)) {
                return std::nullopt;
            }
            return static_cast<std::uint8_t>(mask);
        }

        std::uint8_t mask = 0U;
        std::size_t begin = 0U;
        while (begin <= value.size()) {
            const std::size_t end = value.find_first_of(",;", begin);
            const std::string_view token = value.substr(
                begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
            unsigned long long slot = 0U;
            if (!parse_unsigned(token, 3U, slot)) {
                return std::nullopt;
            }
            mask = static_cast<std::uint8_t>(mask | (1U << slot));
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1U;
        }
        return mask;
    }

    [[nodiscard]] std::optional<std::size_t> parse_msx_ram_size_value(std::string_view value) {
        value = trim_ascii(value);
        if (value.empty()) {
            return std::nullopt;
        }

        unsigned long long multiplier = 1U;
        const char suffix = value.back();
        if (suffix == 'k' || suffix == 'K') {
            multiplier = 1024U;
            value.remove_suffix(1U);
        } else if (suffix == 'm' || suffix == 'M') {
            multiplier = 1024U * 1024U;
            value.remove_suffix(1U);
        }

        constexpr unsigned long long k_min_ram_size = 0x4000ULL;
        constexpr unsigned long long k_max_ram_size = 0x400000ULL;
        unsigned long long units = 0U;
        if (!parse_unsigned(value, k_max_ram_size / multiplier, units)) {
            return std::nullopt;
        }

        const unsigned long long bytes = units * multiplier;
        if (bytes < k_min_ram_size || bytes > k_max_ram_size) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(bytes);
    }

    [[nodiscard]] std::optional<mnemos::frontend_sdk::msx_slot_location>
    parse_msx_slot_location_env(const char* env_name) {
        const char* value = getenv_nonempty(env_name);
        if (value == nullptr) {
            return std::nullopt;
        }
        auto parsed = parse_msx_slot_location_value(value);
        if (!parsed) {
            std::fprintf(stderr,
                         "[mnemos_player] ignoring %s=%s; expected slot as primary or "
                         "primary.secondary\n",
                         env_name, value);
        }
        return parsed;
    }

    [[nodiscard]] std::optional<std::uint8_t> parse_msx_expanded_slots_env(const char* env_name) {
        const char* value = getenv_nonempty(env_name);
        if (value == nullptr) {
            return std::nullopt;
        }
        auto parsed = parse_msx_expanded_slots_value(value);
        if (!parsed) {
            std::fprintf(stderr,
                         "[mnemos_player] ignoring %s=%s; expected expanded-slot mask or "
                         "comma-separated slot list\n",
                         env_name, value);
        }
        return parsed;
    }

    [[nodiscard]] std::optional<std::size_t> parse_msx_ram_size_env(const char* env_name) {
        const char* value = getenv_nonempty(env_name);
        if (value == nullptr) {
            return std::nullopt;
        }
        auto parsed = parse_msx_ram_size_value(value);
        if (!parsed) {
            std::fprintf(stderr,
                         "[mnemos_player] ignoring %s=%s; expected bytes, K, or M RAM size\n",
                         env_name, value);
        }
        return parsed;
    }

    [[nodiscard]] mnemos::frontend_sdk::msx_machine_profile
    parse_msx_machine_profile(std::string_view prefix, bool include_ram_size) {
        const std::string prefix_text(prefix);
        mnemos::frontend_sdk::msx_machine_profile profile{};

        const std::string expanded_name = prefix_text + "EXPANDED_SLOTS";
        const std::string ram_slot_name = prefix_text + "RAM_SLOT";
        const std::string sub_slot_name = prefix_text + "SUB_SLOT";
        const std::string disk_slot_name = prefix_text + "DISK_SLOT";
        const std::string cart2_slot_name = prefix_text + "CART2_SLOT";
        profile.expanded_primary_slots = parse_msx_expanded_slots_env(expanded_name.c_str());
        profile.ram_slot = parse_msx_slot_location_env(ram_slot_name.c_str());
        profile.sub_bios_slot = parse_msx_slot_location_env(sub_slot_name.c_str());
        profile.disk_slot = parse_msx_slot_location_env(disk_slot_name.c_str());
        profile.cartridge2_slot = parse_msx_slot_location_env(cart2_slot_name.c_str());

        if (include_ram_size) {
            const std::string ram_size_name = prefix_text + "RAM_SIZE";
            profile.ram_size = parse_msx_ram_size_env(ram_size_name.c_str());
        }
        return profile;
    }

    constexpr std::size_t k_msx2_main_bios_size = 0x8000U;
    constexpr std::size_t k_msx2_sub_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_logo_rom_size = 0x4000U;
    constexpr std::size_t k_msx_logo_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_disk_rom_size = 0x4000U;
    constexpr std::size_t k_msx2_packed_main_sub_size = k_msx2_main_bios_size + k_msx2_sub_rom_size;
    constexpr std::size_t k_msx2_packed_main_sub_disk_size =
        k_msx2_packed_main_sub_size + k_msx2_disk_rom_size;

    struct msx2_packed_firmware final {
        std::vector<std::uint8_t> main_bios{};
        std::vector<std::uint8_t> sub_rom{};
        std::vector<std::uint8_t> disk_rom{};
    };

    enum class msx_launch_media_kind : std::uint8_t {
        none,
        cartridge,
        disk,
        tape,
    };

    [[nodiscard]] msx_launch_media_kind
    classify_msx_launch_media(std::span<const std::uint8_t> media) noexcept {
        if (media.empty()) {
            return msx_launch_media_kind::none;
        }
        if (mnemos::chips::storage::msx_cassette::has_cas_header(media)) {
            return msx_launch_media_kind::tape;
        }
        if (mnemos::chips::storage::wd1793::is_supported_dsk(media)) {
            return msx_launch_media_kind::disk;
        }
        return msx_launch_media_kind::cartridge;
    }

    [[nodiscard]] bool is_msx_disk_media(std::span<const std::uint8_t> media) noexcept {
        return classify_msx_launch_media(media) == msx_launch_media_kind::disk;
    }

    [[nodiscard]] bool
    media_set_contains_msx_disk(std::span<const std::uint8_t> primary_media,
                                const std::vector<std::vector<std::uint8_t>>& additional_media) {
        if (is_msx_disk_media(primary_media)) {
            return true;
        }
        for (const auto& media : additional_media) {
            if (is_msx_disk_media(media)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const char* msx_media_label(msx_launch_media_kind kind) noexcept {
        switch (kind) {
        case msx_launch_media_kind::disk:
            return "disk";
        case msx_launch_media_kind::tape:
            return "tape";
        case msx_launch_media_kind::cartridge:
            return "cartridge";
        case msx_launch_media_kind::none:
        default:
            return "media";
        }
    }

    [[nodiscard]] bool split_msx2_packed_firmware(const std::vector<std::uint8_t>& image,
                                                  msx2_packed_firmware& out) {
        if (image.size() < k_msx2_packed_main_sub_size) {
            return false;
        }

        const auto main_begin = image.begin();
        const auto sub_begin = main_begin + static_cast<std::ptrdiff_t>(k_msx2_main_bios_size);
        const auto disk_begin = sub_begin + static_cast<std::ptrdiff_t>(k_msx2_sub_rom_size);

        out.main_bios.assign(main_begin, sub_begin);
        out.sub_rom.assign(sub_begin, disk_begin);
        out.disk_rom.clear();
        if (image.size() >= k_msx2_packed_main_sub_disk_size) {
            out.disk_rom.assign(disk_begin,
                                disk_begin + static_cast<std::ptrdiff_t>(k_msx2_disk_rom_size));
        }
        return true;
    }

    void set_bios_image_slot(std::vector<std::vector<std::uint8_t>>& bios_images, std::size_t slot,
                             std::vector<std::uint8_t> image) {
        while (bios_images.size() <= slot) {
            bios_images.push_back({});
        }
        bios_images[slot] = std::move(image);
    }

    [[nodiscard]] std::string
    battery_media_path_for(mnemos::frontend_sdk::player_system& system,
                           const std::vector<std::string>& rom_paths,
                           mnemos::apps::player::adapters::system_family family) {
        if (rom_paths.empty() || system.battery_ram().empty()) {
            return {};
        }

        const std::string_view media_id = system.battery_ram_media_id();
        if ((family == mnemos::apps::player::adapters::system_family::msx ||
             family == mnemos::apps::player::adapters::system_family::msx2) &&
            media_id == "cart2" && rom_paths.size() > 1U) {
            return rom_paths[1];
        }
        return rom_paths.front();
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

    [[nodiscard]] std::string
    amiga_keyboard_layout_override(const std::optional<std::string>& override) {
        std::string layout = override.value_or(std::string{});
        if (!layout.empty()) {
            return layout;
        }
        if (const char* layout_env = MNEMOS_PLAYER_GETENV("MNEMOS_AMIGA500_KEYBOARD_LAYOUT");
            layout_env != nullptr && layout_env[0] != '\0') {
            layout = layout_env;
        }
        return layout;
    }

    [[nodiscard]] std::string
    amiga_model_override(const std::optional<std::string>& override,
                         mnemos::apps::player::adapters::system_family family) {
        if (override.has_value() && !override->empty()) {
            return *override;
        }
        if (family == mnemos::apps::player::adapters::system_family::amiga2000) {
            if (const char* model_env = MNEMOS_PLAYER_GETENV("MNEMOS_AMIGA2000_MODEL");
                model_env != nullptr && model_env[0] != '\0') {
                return std::string{model_env};
            }
        }
        if (const char* model_env = MNEMOS_PLAYER_GETENV("MNEMOS_AMIGA_MODEL");
            model_env != nullptr && model_env[0] != '\0') {
            return std::string{model_env};
        }
        return {};
    }

    [[nodiscard]] bool
    is_amiga_family(mnemos::apps::player::adapters::system_family family) noexcept {
        using mnemos::apps::player::adapters::system_family;
        return family == system_family::amiga500 || family == system_family::amiga500_plus ||
               family == system_family::amiga600 || family == system_family::amiga2000;
    }

    [[nodiscard]] const char*
    amiga_kickstart_env_var(mnemos::apps::player::adapters::system_family family) noexcept {
        using mnemos::apps::player::adapters::system_family;
        switch (family) {
        case system_family::amiga500_plus:
            return "MNEMOS_AMIGA500PLUS_KICKSTART";
        case system_family::amiga600:
            return "MNEMOS_AMIGA600_KICKSTART";
        case system_family::amiga2000:
            return "MNEMOS_AMIGA2000_KICKSTART";
        default:
            return "MNEMOS_AMIGA500_KICKSTART";
        }
    }

    [[nodiscard]] const char*
    amiga_bios_env_var(mnemos::apps::player::adapters::system_family family) noexcept {
        using mnemos::apps::player::adapters::system_family;
        switch (family) {
        case system_family::amiga500_plus:
            return "MNEMOS_AMIGA500PLUS_BIOS";
        case system_family::amiga600:
            return "MNEMOS_AMIGA600_BIOS";
        case system_family::amiga2000:
            return "MNEMOS_AMIGA2000_BIOS";
        default:
            return "MNEMOS_AMIGA500_BIOS";
        }
    }

    [[nodiscard]] const char*
    amiga_kickstart_dir_env_var(mnemos::apps::player::adapters::system_family family) noexcept {
        using mnemos::apps::player::adapters::system_family;
        switch (family) {
        case system_family::amiga500_plus:
            return "MNEMOS_AMIGA500PLUS_KICKSTART_DIR";
        case system_family::amiga600:
            return "MNEMOS_AMIGA600_KICKSTART_DIR";
        case system_family::amiga2000:
            return "MNEMOS_AMIGA2000_KICKSTART_DIR";
        default:
            return "MNEMOS_AMIGA500_KICKSTART_DIR";
        }
    }

    [[nodiscard]] const char*
    amiga_bios_dir_env_var(mnemos::apps::player::adapters::system_family family) noexcept {
        using mnemos::apps::player::adapters::system_family;
        switch (family) {
        case system_family::amiga500_plus:
            return "MNEMOS_AMIGA500PLUS_BIOS_DIR";
        case system_family::amiga600:
            return "MNEMOS_AMIGA600_BIOS_DIR";
        case system_family::amiga2000:
            return "MNEMOS_AMIGA2000_BIOS_DIR";
        default:
            return "MNEMOS_AMIGA500_BIOS_DIR";
        }
    }

    [[nodiscard]] std::span<const std::string_view> amiga_kickstart_filename_candidates(
        mnemos::apps::player::adapters::system_family family) noexcept {
        using mnemos::apps::player::adapters::system_family;
        static constexpr std::string_view a500[] = {
            "Kickstart 1.3.rom", "Kickstart 1.2.rom", "kick13.rom",       "kick12.rom",
            "kickstart13.rom",   "kickstart12.rom",   "kickstart1.3.rom", "kickstart1.2.rom"};
        static constexpr std::string_view a500plus[] = {
            "Kickstart 2.0.rom", "Kickstart 2.04.rom", "Kickstart 2.05.rom", "kick20.rom",
            "kick204.rom",       "kick205.rom",        "kickstart20.rom",    "kickstart2.0.rom"};
        static constexpr std::string_view a600[] = {
            "Kickstart 2.0.rom", "Kickstart 2.05.rom", "Kickstart 3.1.rom",
            "kick20.rom",        "kick205.rom",        "kick31.rom",
            "kickstart20.rom",   "kickstart2.0.rom",   "kickstart31.rom"};
        switch (family) {
        case system_family::amiga500_plus:
            return a500plus;
        case system_family::amiga600:
            return a600;
        case system_family::amiga2000:
            return a500;
        default:
            return a500;
        }
    }

    [[nodiscard]] std::optional<std::string>
    find_amiga_kickstart_in_dir(std::string_view dir,
                                mnemos::apps::player::adapters::system_family family) {
        if (dir.empty()) {
            return std::nullopt;
        }
        const std::filesystem::path root{std::string{dir}};
        for (std::string_view name : amiga_kickstart_filename_candidates(family)) {
            const std::filesystem::path candidate = root / std::filesystem::path{std::string{name}};
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate.string();
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string>
    amiga_kickstart_path(mnemos::apps::player::adapters::system_family family) {
        if (const char* kickstart_env = getenv_nonempty(amiga_kickstart_env_var(family));
            kickstart_env != nullptr) {
            return std::string{kickstart_env};
        }
        if (const char* bios_env = getenv_nonempty(amiga_bios_env_var(family));
            bios_env != nullptr) {
            return std::string{bios_env};
        }

        const char* dirs[] = {amiga_kickstart_dir_env_var(family), amiga_bios_dir_env_var(family),
                              "MNEMOS_AMIGA_KICKSTART_DIR", "MNEMOS_AMIGA_BIOS_DIR"};
        for (const char* env_name : dirs) {
            if (const char* dir = getenv_nonempty(env_name); dir != nullptr) {
                if (auto path = find_amiga_kickstart_in_dir(dir, family); path.has_value()) {
                    return path;
                }
            }
        }
        return std::nullopt;
    }

    void print_missing_amiga_kickstart(mnemos::apps::player::adapters::system_family family,
                                       std::string_view media_kind) {
        std::fprintf(stderr,
                     "[mnemos_player] %.*s %s needs %s (or %s) set to a Kickstart ROM, "
                     "or MNEMOS_AMIGA_BIOS_DIR containing a model-appropriate Kickstart ROM\n",
                     static_cast<int>(media_kind.size()), media_kind.data(),
                     mnemos::apps::player::adapters::family_label(family),
                     amiga_kickstart_env_var(family), amiga_bios_env_var(family));
    }

    [[nodiscard]] bool
    validate_amiga_adf_images(std::span<const mnemos::apps::player::adapters::loaded_rom> disks,
                              const std::string& source_path) {
        constexpr std::size_t expected_size =
            mnemos::manifests::amiga::amiga_system::floppy_dd_size;
        for (const auto& disk : disks) {
            if (disk.bytes.size() == expected_size) {
                continue;
            }
            const char* name = disk.name.empty() ? source_path.c_str() : disk.name.c_str();
            std::fprintf(stderr,
                         "[mnemos_player] unsupported Amiga ADF image '%s' from %s: %zu bytes "
                         "(expected %zu-byte standard DD ADF; extended/raw/IPF track images are "
                         "not supported yet)\n",
                         name, source_path.c_str(), disk.bytes.size(), expected_size);
            return false;
        }
        return !disks.empty();
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
        using adapters::load_rom_entries_by_extension;
        using adapters::load_rom_verbatim;
        using adapters::resolve_video_region;
        using adapters::system_family;

        system_launch_outcome outcome{};
        if (options.rom_paths.empty()) {
            if (!options.system_arg) {
                return outcome;
            }
            const auto family_opt = family_from_name(*options.system_arg);
            if (!family_opt || !is_amiga_family(*family_opt)) {
                return outcome;
            }

            const system_family family = *family_opt;
            auto kickstart_path = amiga_kickstart_path(family);
            if (!kickstart_path) {
                print_missing_amiga_kickstart(family, "BIOS-only launch for");
                outcome.exit_code = 1;
                return outcome;
            }
            auto kickstart = load_rom(*kickstart_path);
            if (!kickstart || kickstart->bytes.empty()) {
                std::fprintf(stderr, "[mnemos_player] could not read Amiga Kickstart ROM: %s\n",
                             kickstart_path->c_str());
                outcome.exit_code = 1;
                return outcome;
            }

            const auto video =
                resolve_video_region(options.region_override, mnemos::video_region::pal);
            std::fprintf(stderr, "[mnemos_player] system: %s  region: %s (%s)\n",
                         family_label(family), video == mnemos::video_region::pal ? "PAL" : "NTSC",
                         adapters::region_source_label(options.region_override));
            std::fflush(stderr);

            force_link_all_adapters();

            frontend_sdk::adapter_options adapter_options{};
            adapter_options.rom = std::move(kickstart->bytes);
            adapter_options.video_region = video;
            adapter_options.display_name = clean_rom_name(kickstart->name);
            adapter_options.autostart = options.autostart;
            adapter_options.dip_override = options.dip_override;
            adapter_options.mapper_override = options.mapper_override.value_or(std::string{});
            adapter_options.mapper2_override = options.mapper2_override.value_or(std::string{});
            adapter_options.fm_unit = options.fm_unit;
            adapter_options.light_gun = options.light_gun;
            adapter_options.four_score = options.four_score;
            adapter_options.rtc = options.rtc;
            adapter_options.msx2 = options.msx2;
            adapter_options.keyboard_layout_override =
                amiga_keyboard_layout_override(options.keyboard_layout_override);
            adapter_options.amiga_model_override =
                amiga_model_override(options.amiga_model_override, family);
            outcome.system = frontend_sdk::adapter_registry::instance().create(
                family_id(family), std::move(adapter_options));
            if (!outcome.system) {
                std::fprintf(stderr, "[mnemos_player] no adapter registered for family '%s'\n",
                             family_id(family));
                outcome.exit_code = 1;
                return outcome;
            }
            log_media_validation_issues(*outcome.system);
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
            family == system_family::irem_m10 || family == system_family::irem_m14 ||
            family == system_family::irem_m15 || family == system_family::irem_m27 ||
            family == system_family::irem_m47 || family == system_family::irem_m52 ||
            family == system_family::irem_m57 || family == system_family::irem_m58 ||
            family == system_family::irem_m62 || family == system_family::irem_m63 ||
            family == system_family::irem_travrusa || family == system_family::irem_redalert ||
            family == system_family::irem_m72 || family == system_family::irem_m75 ||
            family == system_family::irem_m78 || family == system_family::irem_m81 ||
            family == system_family::irem_m82 || family == system_family::irem_m84 ||
            family == system_family::irem_m85 || family == system_family::irem_m90 ||
            family == system_family::irem_m92 || family == system_family::irem_m102 ||
            family == system_family::irem_m107 || family == system_family::irem_m119 ||
            family == system_family::taito_f2 || family == system_family::taito_gnet ||
            family == system_family::capcom_cps1 || family == system_family::capcom_cps2;
        auto loaded = arcade_family ? load_rom_verbatim(options.rom_paths.front())
                                    : load_rom(options.rom_paths.front());
        const bool directory_backed_irem =
            loaded && loaded->directory_source &&
            (family == system_family::irem_m10 || family == system_family::irem_m14 ||
             family == system_family::irem_m15 || family == system_family::irem_m27 ||
             family == system_family::irem_m47 || family == system_family::irem_m52 ||
             family == system_family::irem_m57 || family == system_family::irem_m58 ||
             family == system_family::irem_m62 || family == system_family::irem_m63 ||
             family == system_family::irem_travrusa || family == system_family::irem_redalert ||
             family == system_family::irem_m72 || family == system_family::irem_m75 ||
             family == system_family::irem_m78 || family == system_family::irem_m81 ||
             family == system_family::irem_m82 || family == system_family::irem_m84 ||
             family == system_family::irem_m85 || family == system_family::irem_m90 ||
             family == system_family::irem_m92 || family == system_family::irem_m102 ||
             family == system_family::irem_m107 || family == system_family::irem_m119);
        if (!loaded || (loaded->bytes.empty() && !directory_backed_irem)) {
            std::fprintf(stderr, "could not read ROM: %s\n", options.rom_paths.front().c_str());
            outcome.exit_code = 1;
            return outcome;
        }

        std::vector<std::vector<std::uint8_t>> additional_media;
        std::vector<std::string> additional_media_paths;
        for (std::size_t i = 1; i < options.rom_paths.size(); ++i) {
            const bool irem_family =
                family == system_family::irem_m10 || family == system_family::irem_m14 ||
                family == system_family::irem_m15 || family == system_family::irem_m27 ||
                family == system_family::irem_m47 || family == system_family::irem_m52 ||
                family == system_family::irem_m57 || family == system_family::irem_m58 ||
                family == system_family::irem_m62 || family == system_family::irem_m63 ||
                family == system_family::irem_travrusa || family == system_family::irem_redalert ||
                family == system_family::irem_m72 || family == system_family::irem_m75 ||
                family == system_family::irem_m78 || family == system_family::irem_m81 ||
                family == system_family::irem_m82 || family == system_family::irem_m84 ||
                family == system_family::irem_m85 || family == system_family::irem_m90 ||
                family == system_family::irem_m92 || family == system_family::irem_m102 ||
                family == system_family::irem_m107 || family == system_family::irem_m119;
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
        case system_family::amiga500_plus:
        case system_family::amiga600:
        case system_family::amiga2000:
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
        case system_family::irem_m10:
        case system_family::irem_m14:
        case system_family::irem_m15:
        case system_family::irem_m27:
        case system_family::irem_m47:
        case system_family::irem_m52:
        case system_family::irem_m57:
        case system_family::irem_m58:
        case system_family::irem_m62:
        case system_family::irem_m63:
        case system_family::irem_travrusa:
        case system_family::irem_redalert:
        case system_family::irem_m72:
        case system_family::irem_m75:
        case system_family::irem_m78:
        case system_family::irem_m81:
        case system_family::irem_m82:
        case system_family::irem_m84:
        case system_family::irem_m85:
        case system_family::irem_m90:
        case system_family::irem_m92:
        case system_family::irem_m102:
        case system_family::irem_m107:
        case system_family::irem_m119:
        case system_family::taito_f2:
        case system_family::taito_gnet:
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
        std::string display_name = clean_rom_name(loaded->name);
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
        std::string keyboard_layout_override =
            options.keyboard_layout_override.value_or(std::string{});
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

        if (family == system_family::taito_gnet) {
            const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_TAITO_GNET_BIOS");
            if (bios_env == nullptr) {
                std::fprintf(stderr, "[mnemos_player] Taito G-NET needs "
                                     "MNEMOS_TAITO_GNET_BIOS set to a ZN-2/G-NET BIOS ROM\n");
                outcome.exit_code = 1;
                return outcome;
            }
            auto bios = load_rom(bios_env);
            if (!bios || bios->bytes.empty()) {
                std::fprintf(stderr, "[mnemos_player] could not read Taito G-NET BIOS: %s\n",
                             bios_env);
                outcome.exit_code = 1;
                return outcome;
            }
            bios_images.push_back(std::move(bios->bytes));
        }

        if (family == system_family::msx) {
            const char* bios_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX_BIOS");
            const msx_launch_media_kind primary_kind = classify_msx_launch_media(primary_rom);
            if (bios_env != nullptr && bios_env[0] != '\0') {
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < 0x8000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX BIOS: %s\n", bios_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom = std::move(bios->bytes);
            } else if (primary_kind == msx_launch_media_kind::disk ||
                       primary_kind == msx_launch_media_kind::tape) {
                std::fprintf(stderr,
                             "[mnemos_player] an MSX %s needs MNEMOS_MSX_BIOS set to "
                             "a system BIOS ROM\n",
                             msx_media_label(primary_kind));
                outcome.exit_code = 1;
                return outcome;
            } else {
                std::fprintf(stderr, "[mnemos_player] MNEMOS_MSX_BIOS unset; treating --rom as "
                                     "the MSX BIOS image (no cartridge mounted)\n");
            }
            const bool disk_requested = media_set_contains_msx_disk(primary_rom, additional_media);
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
                set_bios_image_slot(bios_images, 0U, std::move(disk_rom->bytes));
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
                set_bios_image_slot(bios_images, 1U, std::move(kanji_rom->bytes));
            }
            if (const char* logo_rom_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX_LOGO_ROM");
                logo_rom_env != nullptr && logo_rom_env[0] != '\0') {
                auto logo_rom = load_rom(logo_rom_env);
                if (!logo_rom || logo_rom->bytes.size() < k_msx_logo_rom_size) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX logo ROM: %s\n",
                                 logo_rom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                set_bios_image_slot(bios_images, 2U, std::move(logo_rom->bytes));
            }
            if (const char* cas_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX_CAS");
                cas_env != nullptr && cas_env[0] != '\0') {
                auto cas = load_rom(cas_env);
                if (!cas || cas->bytes.empty()) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX CAS image: %s\n",
                                 cas_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                additional_media.push_back(std::move(cas->bytes));
            }
        }

        if (is_amiga_family(family)) {
            keyboard_layout_override =
                amiga_keyboard_layout_override(options.keyboard_layout_override);
            constexpr std::string_view amiga_disk_extensions[] = {".adf", ".adz", ".gz"};
            if (auto disks =
                    load_rom_entries_by_extension(options.rom_paths.front(), amiga_disk_extensions);
                disks.has_value()) {
                if (!validate_amiga_adf_images(*disks, options.rom_paths.front())) {
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto kickstart_path = amiga_kickstart_path(family);
                if (!kickstart_path) {
                    print_missing_amiga_kickstart(family, "a disk image for");
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto kickstart = load_rom(*kickstart_path);
                if (!kickstart || kickstart->bytes.empty()) {
                    std::fprintf(stderr, "[mnemos_player] could not read Amiga Kickstart ROM: %s\n",
                                 kickstart_path->c_str());
                    outcome.exit_code = 1;
                    return outcome;
                }
                display_name = clean_rom_name(disks->front().name);
                std::vector<std::vector<std::uint8_t>> primary_disks;
                primary_disks.reserve(disks->size());
                for (auto& disk : *disks) {
                    primary_disks.push_back(std::move(disk.bytes));
                }
                additional_media.insert(additional_media.begin(),
                                        std::make_move_iterator(primary_disks.begin()),
                                        std::make_move_iterator(primary_disks.end()));
                primary_rom = std::move(kickstart->bytes);
            }
        }

        if (family == system_family::msx2) {
            const msx_launch_media_kind primary_kind = classify_msx_launch_media(primary_rom);
            if (primary_kind == msx_launch_media_kind::disk) {
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom.clear();
            }
            const bool disk_requested = media_set_contains_msx_disk(primary_rom, additional_media);

            std::vector<std::uint8_t> packed_subrom;
            std::vector<std::uint8_t> packed_diskrom;
            if (const char* firmware_env = getenv_nonempty("MNEMOS_MSX2_FIRMWARE");
                firmware_env != nullptr) {
                auto firmware = load_rom(firmware_env);
                msx2_packed_firmware split{};
                if (!firmware || !split_msx2_packed_firmware(firmware->bytes, split)) {
                    std::fprintf(stderr,
                                 "[mnemos_player] could not read packed MSX2 firmware: %s "
                                 "(expected main BIOS + sub-ROM)\n",
                                 firmware_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                bios_images.push_back(std::move(split.main_bios));
                packed_subrom = std::move(split.sub_rom);
                packed_diskrom = std::move(split.disk_rom);
            } else {
                const char* bios_env = getenv_nonempty("MNEMOS_MSX2_BIOS");
                if (bios_env == nullptr) {
                    std::fprintf(stderr, "[mnemos_player] MSX2 needs MNEMOS_MSX2_FIRMWARE or "
                                         "MNEMOS_MSX2_BIOS set to a main BIOS ROM\n");
                    outcome.exit_code = 1;
                    return outcome;
                }
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < k_msx2_main_bios_size) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 BIOS: %s\n",
                                 bios_env);
                    outcome.exit_code = 1;
                    return outcome;
                }

                msx2_packed_firmware split{};
                if (split_msx2_packed_firmware(bios->bytes, split)) {
                    bios_images.push_back(std::move(split.main_bios));
                    packed_subrom = std::move(split.sub_rom);
                    packed_diskrom = std::move(split.disk_rom);
                } else {
                    bios_images.push_back(std::move(bios->bytes));
                }
            }

            if (const char* subrom_env =
                    getenv_nonempty("MNEMOS_MSX2_SUBROM", "MNEMOS_MSX2_SUB_ROM");
                subrom_env != nullptr) {
                auto subrom = load_rom(subrom_env);
                if (!subrom || subrom->bytes.size() < k_msx2_sub_rom_size) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 sub-ROM: %s\n",
                                 subrom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                set_bios_image_slot(bios_images, 1U, std::move(subrom->bytes));
            } else if (!packed_subrom.empty()) {
                set_bios_image_slot(bios_images, 1U, std::move(packed_subrom));
            }

            if (const char* logo_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX2_LOGO_ROM");
                logo_env != nullptr && logo_env[0] != '\0') {
                auto logo = load_rom(logo_env);
                if (!logo || logo->bytes.size() < k_msx2_logo_rom_size) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 logo ROM: %s\n",
                                 logo_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                set_bios_image_slot(bios_images, 4U, std::move(logo->bytes));
            }

            const char* diskrom_env =
                getenv_nonempty("MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM");
            if (disk_requested && diskrom_env == nullptr && packed_diskrom.empty()) {
                std::fprintf(stderr, "[mnemos_player] an MSX2 disk needs "
                                     "MNEMOS_MSX2_DISK_ROM set to a disk interface ROM\n");
                outcome.exit_code = 1;
                return outcome;
            }
            if (diskrom_env != nullptr) {
                auto diskrom = load_rom(diskrom_env);
                if (!diskrom || diskrom->bytes.size() < k_msx2_disk_rom_size) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 disk ROM: %s\n",
                                 diskrom_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                set_bios_image_slot(bios_images, 2U, std::move(diskrom->bytes));
            } else if (!packed_diskrom.empty()) {
                set_bios_image_slot(bios_images, 2U, std::move(packed_diskrom));
            }

            if (const char* kanji_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX2_KANJI_ROM");
                kanji_env != nullptr && kanji_env[0] != '\0') {
                auto kanji = load_rom(kanji_env);
                if (!kanji || kanji->bytes.size() < 0x20000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 Kanji ROM: %s\n",
                                 kanji_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                set_bios_image_slot(bios_images, 3U, std::move(kanji->bytes));
            }
            if (const char* cas_env = MNEMOS_PLAYER_GETENV("MNEMOS_MSX2_CAS");
                cas_env != nullptr && cas_env[0] != '\0') {
                auto cas = load_rom(cas_env);
                if (!cas || cas->bytes.empty()) {
                    std::fprintf(stderr, "[mnemos_player] could not read MSX2 CAS image: %s\n",
                                 cas_env);
                    outcome.exit_code = 1;
                    return outcome;
                }
                additional_media.push_back(std::move(cas->bytes));
            }
        }

        mnemos::frontend_sdk::msx_machine_profile msx_profile{};
        if (family == system_family::msx) {
            msx_profile = parse_msx_machine_profile("MNEMOS_MSX_", true);
        } else if (family == system_family::msx2) {
            msx_profile = parse_msx_machine_profile("MNEMOS_MSX2_", true);
        }

        outcome.system = frontend_sdk::adapter_registry::instance().create(
            family_id(family),
            {.rom = std::move(primary_rom),
             .video_region = video,
             .display_name = std::move(display_name),
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
             .msx_profile = msx_profile,
             .disc_path = std::move(disc_path),
             .rom_path = options.rom_paths.front(),
             .bios_images = std::move(bios_images),
             .keyboard_layout_override = std::move(keyboard_layout_override),
             .amiga_model_override = amiga_model_override(options.amiga_model_override, family)});
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
        outcome.battery_media_path =
            battery_media_path_for(*outcome.system, options.rom_paths, family);
        return outcome;
    }

} // namespace mnemos::apps::player

#if defined(MNEMOS_PLAYER_GETENV)
#undef MNEMOS_PLAYER_GETENV
#endif
