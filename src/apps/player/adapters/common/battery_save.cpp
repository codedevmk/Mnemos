#include "battery_save.hpp"

#include "file.hpp" // mnemos::io::read_file / write_file

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace mnemos::apps::player::adapters {

    std::string srm_path_for(std::string_view rom_path) {
        const auto slash = rom_path.find_last_of("/\\");
        const auto begin = slash == std::string_view::npos ? std::size_t{0} : slash + 1U;
        const auto dot = rom_path.find_last_of('.');
        // Only a dot strictly inside the basename marks an extension; a leading
        // dot ("/d/.sram") belongs to the name itself, not a suffix to replace.
        const auto stem_end =
            (dot == std::string_view::npos || dot <= begin) ? rom_path.size() : dot;
        std::string out(rom_path.substr(0, stem_end));
        out += ".srm";
        return out;
    }

    bool load_battery_ram(const std::string& srm_path, std::span<std::uint8_t> dst) {
        if (dst.empty()) {
            return false;
        }
        const auto bytes = mnemos::io::read_file(srm_path);
        if (!bytes) {
            return false;
        }
        const std::size_t n = std::min(bytes->size(), dst.size());
        std::copy_n(bytes->begin(), n, dst.begin());
        return true;
    }

    bool save_battery_ram(const std::string& srm_path, std::span<const std::uint8_t> bytes) {
        if (bytes.empty()) {
            return false;
        }
        const std::string tmp = srm_path + ".tmp";
        if (!mnemos::io::write_file(tmp, bytes)) {
            return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, srm_path, ec);
        if (ec) {
            std::error_code rm;
            std::filesystem::remove(tmp, rm); // don't leave the temp behind
            return false;
        }
        return true;
    }

} // namespace mnemos::apps::player::adapters
