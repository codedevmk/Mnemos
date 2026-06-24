#include "state_file.hpp"

#include "file.hpp" // mnemos::io::read_file / write_file

#include <filesystem>
#include <system_error>

namespace mnemos::apps::player::adapters {

    std::string state_path_for(std::string_view rom_path) {
        const auto slash = rom_path.find_last_of("/\\");
        const auto begin = slash == std::string_view::npos ? std::size_t{0} : slash + 1U;
        const auto dot = rom_path.find_last_of('.');
        const auto stem_end =
            (dot == std::string_view::npos || dot <= begin) ? rom_path.size() : dot;
        std::string out(rom_path.substr(0, stem_end));
        out += ".mns";
        return out;
    }

    std::optional<std::vector<std::uint8_t>> load_save_state_file(const std::string& path) {
        return mnemos::io::read_file(path);
    }

    bool save_save_state_file(const std::string& path, std::span<const std::uint8_t> bytes) {
        if (bytes.empty()) {
            return false;
        }
        const std::string tmp = path + ".tmp";
        if (!mnemos::io::write_file(tmp, bytes)) {
            return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::error_code rm;
            std::filesystem::remove(tmp, rm);
            return false;
        }
        return true;
    }

} // namespace mnemos::apps::player::adapters
