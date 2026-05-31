#include "encoding.hpp"

namespace mnemos::text {

    std::vector<std::uint8_t> encode_latin1(std::string_view text) {
        std::vector<std::uint8_t> out;
        out.reserve(text.size());
        for (const char c : text) {
            out.push_back(static_cast<std::uint8_t>(c));
        }
        return out;
    }

} // namespace mnemos::text
