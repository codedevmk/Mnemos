#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::apps::player {

    struct amiga_ipf_decode_result final {
        std::vector<std::uint8_t> image{};
        std::string error{};

        [[nodiscard]] bool ok() const noexcept { return !image.empty() && error.empty(); }
    };

    [[nodiscard]] amiga_ipf_decode_result
    decode_amiga_ipf_to_extended_adf(std::span<const std::uint8_t> ipf_image,
                                     std::string_view display_name);

} // namespace mnemos::apps::player
