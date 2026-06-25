#include "amiga500_adapter.hpp"
#include "amiga500_system.hpp"
#include "deflate.hpp"
#include "system_launch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    namespace fs = std::filesystem;

    using mnemos::frontend_sdk::player_system;
    using mnemos::apps::player::adapters::amiga500::amiga500_adapter;
    using mnemos::manifests::amiga500::amiga500_system;

    struct temp_path_guard final {
        fs::path path;

        ~temp_path_guard() {
            std::error_code ec;
            fs::remove(path, ec);
        }
    };

    [[nodiscard]] std::optional<std::string> read_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string{value};
    }

    [[nodiscard]] bool set_env_value(const std::string& name, const std::string& value) {
#if defined(_WIN32)
        return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
        return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
    }

    [[nodiscard]] bool clear_env_value(const std::string& name) {
#if defined(_WIN32)
        return _putenv_s(name.c_str(), "") == 0;
#else
        return unsetenv(name.c_str()) == 0;
#endif
    }

    struct env_var_guard final {
        std::string name;
        std::optional<std::string> previous;

        env_var_guard(std::string env_name, std::string value)
            : name(std::move(env_name)), previous(read_env(name.c_str())) {
            REQUIRE(set_env_value(name, value));
        }

        ~env_var_guard() {
            if (previous) {
                (void)set_env_value(name, *previous);
            } else {
                (void)clear_env_value(name);
            }
        }
    };

    [[nodiscard]] std::vector<std::uint8_t> tiny_kickstart() {
        std::vector<std::uint8_t> rom(amiga500_system::kickstart_window_size, 0x00U);
        const auto w16 = [&](std::size_t offset, std::uint16_t value) {
            rom[offset] = static_cast<std::uint8_t>(value >> 8U);
            rom[offset + 1U] = static_cast<std::uint8_t>(value);
        };
        const auto w32 = [&](std::size_t offset, std::uint32_t value) {
            rom[offset + 0U] = static_cast<std::uint8_t>(value >> 24U);
            rom[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
            rom[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
            rom[offset + 3U] = static_cast<std::uint8_t>(value);
        };
        w32(0x0000U, 0x0007F000U);
        w32(0x0004U, amiga500_system::kickstart_base + 0x0008U);
        w16(0x0008U, 0x46FCU); // MOVE #$2700,SR
        w16(0x000AU, 0x2700U);
        w16(0x000CU, 0x60FEU); // BRA.S self
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_adf() {
        std::vector<std::uint8_t> adf(amiga500_system::floppy_dd_size, 0x00U);
        adf[0] = 0x44U;
        adf[1] = 0x89U;
        return adf;
    }

    void put16_le(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32_le(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16_le(out, static_cast<std::uint16_t>(v));
        put16_le(out, static_cast<std::uint16_t>(v >> 16U));
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_zip(const std::string& name, const std::vector<std::uint8_t>& data,
             std::uint16_t method, const std::vector<std::uint8_t>& payload) {
        std::vector<std::uint8_t> out;
        const auto local_offset = static_cast<std::uint32_t>(out.size());
        const auto compressed_size = static_cast<std::uint32_t>(payload.size());
        const auto uncompressed_size = static_cast<std::uint32_t>(data.size());
        put32_le(out, 0x04034B50U);
        put16_le(out, 20U);
        put16_le(out, 0U);
        put16_le(out, method);
        put32_le(out, 0U);
        put32_le(out, 0U); // CRC is not consumed by the ZIP reader.
        put32_le(out, compressed_size);
        put32_le(out, uncompressed_size);
        put16_le(out, static_cast<std::uint16_t>(name.size()));
        put16_le(out, 0U);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), payload.begin(), payload.end());

        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        put32_le(out, 0x02014B50U);
        put16_le(out, 20U);
        put16_le(out, 20U);
        put16_le(out, 0U);
        put16_le(out, method);
        put32_le(out, 0U);
        put32_le(out, 0U);
        put32_le(out, compressed_size);
        put32_le(out, uncompressed_size);
        put16_le(out, static_cast<std::uint16_t>(name.size()));
        put16_le(out, 0U);
        put16_le(out, 0U);
        put16_le(out, 0U);
        put16_le(out, 0U);
        put32_le(out, 0U);
        put32_le(out, local_offset);
        out.insert(out.end(), name.begin(), name.end());

        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32_le(out, 0x06054B50U);
        put16_le(out, 0U);
        put16_le(out, 0U);
        put16_le(out, 1U);
        put16_le(out, 1U);
        put32_le(out, cd_size);
        put32_le(out, cd_offset);
        put16_le(out, 0U);
        return out;
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_deflated_zip(const std::string& name, const std::vector<std::uint8_t>& data) {
        return make_zip(name, data, 8U, mnemos::compression::deflate_raw(data));
    }

    [[nodiscard]] fs::path write_temp_file(std::string_view tag, std::string_view suffix,
                                           const std::vector<std::uint8_t>& bytes) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path path =
            fs::temp_directory_path() /
            ("mnemos_amiga500_launch_test_" + std::string{tag} + "_" + std::to_string(stamp) +
             std::string{suffix});
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
        return path;
    }

    [[nodiscard]] fs::path write_temp_rom(const std::vector<std::uint8_t>& rom) {
        return write_temp_file("kickstart", ".rom", rom);
    }

    [[nodiscard]] bool spec_has_field(const player_system& system, std::string_view label,
                                      std::string_view value) {
        for (const auto& field : system.system_spec()) {
            if (field.label == label && field.value == value) {
                return true;
            }
        }
        return false;
    }

} // namespace

TEST_CASE("player launch boots Amiga500 from Kickstart env without disk media",
          "[apps][player][launch][amiga500]") {
    temp_path_guard rom_path{write_temp_rom(tiny_kickstart())};
    env_var_guard kickstart{"MNEMOS_AMIGA500_KICKSTART", rom_path.path.string()};
    env_var_guard layout{"MNEMOS_AMIGA500_KEYBOARD_LAYOUT", "azerty"};

    auto outcome =
        mnemos::apps::player::launch_system({.system_arg = std::string{"amiga500"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path.empty());
    CHECK(outcome.system->media_count() == 0U);
    CHECK(spec_has_field(*outcome.system, "BIOS", rom_path.path.stem().string()));
    CHECK(spec_has_field(*outcome.system, "Keyboard", "AZERTY"));
}

TEST_CASE("player launch treats a zip-wrapped Amiga ADF as disk media",
          "[apps][player][launch][amiga500]") {
    temp_path_guard rom_path{write_temp_rom(tiny_kickstart())};
    temp_path_guard disk_path{
        write_temp_file("disk", ".zip", make_deflated_zip("Workbench.adf", tiny_adf()))};
    env_var_guard kickstart{"MNEMOS_AMIGA500_KICKSTART", rom_path.path.string()};

    auto outcome =
        mnemos::apps::player::launch_system({.rom_paths = {disk_path.path.string()},
                                             .system_arg = std::string{"amiga500"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path == disk_path.path.string());
    CHECK(outcome.system->media_count() == 1U);
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().floppy_loaded());
    CHECK(adapter->system().floppy_size() == amiga500_system::floppy_dd_size);
    CHECK_FALSE(adapter->system().floppy_drives[0].change_latch);
    CHECK(spec_has_field(*outcome.system, "Disk", "Workbench"));
}

TEST_CASE("player launch passes keyboard layout override to Amiga500 adapter",
          "[apps][player][launch][amiga500]") {
    temp_path_guard rom_path{write_temp_rom(tiny_kickstart())};

    auto outcome =
        mnemos::apps::player::launch_system({.rom_paths = {rom_path.path.string()},
                                             .system_arg = std::string{"amiga500"},
                                             .keyboard_layout_override = std::string{"azerty"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(spec_has_field(*outcome.system, "Keyboard", "AZERTY"));
}
