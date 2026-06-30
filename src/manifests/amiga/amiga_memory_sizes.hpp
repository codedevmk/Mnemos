#pragma once

#include <cstddef>

namespace mnemos::manifests::amiga {

    inline constexpr std::size_t amiga_size_256k = 256U * 1024U;
    inline constexpr std::size_t amiga_size_512k = amiga_size_256k * 2U;
    inline constexpr std::size_t amiga_size_1m = amiga_size_512k * 2U;
    inline constexpr std::size_t amiga_size_2m = amiga_size_1m * 2U;
    inline constexpr std::size_t amiga_size_4m = amiga_size_2m * 2U;
    inline constexpr std::size_t amiga_size_8m = amiga_size_4m * 2U;

} // namespace mnemos::manifests::amiga
