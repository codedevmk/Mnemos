#include "decoding.hpp"

namespace mnemos::text {

    std::string decode_latin1(std::span<const std::uint8_t> bytes) {
        std::string out;
        out.reserve(bytes.size());
        for (const std::uint8_t b : bytes) {
            out.push_back(static_cast<char>(b));
        }
        return out;
    }

} // namespace mnemos::text
