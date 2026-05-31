#include "file.hpp"

#include <fstream>
#include <iterator>

namespace mnemos::io {

    std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>()};
    }

    bool write_file(const std::string& path, std::span<const std::uint8_t> bytes) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return out.good();
    }

} // namespace mnemos::io
