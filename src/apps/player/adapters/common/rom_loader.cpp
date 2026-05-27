#include "rom_loader.hpp"

#include <fstream>
#include <iterator>

namespace mnemos::apps::player::adapters {

    std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
    }

    std::string clean_rom_name(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        const auto begin = slash == std::string::npos ? 0U : slash + 1U;
        const auto dot = path.find_last_of('.');
        const auto end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
        return path.substr(begin, end - begin);
    }

} // namespace mnemos::apps::player::adapters
