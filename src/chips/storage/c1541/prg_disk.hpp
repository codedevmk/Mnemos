#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::storage::c1541 {

    // Author an in-memory 35-track .d64 holding a single closed PRG file built
    // from `prg` (the raw program bytes, INCLUDING the 2-byte little-endian load
    // address) under `name` (PETSCII, truncated/0xA0-padded to 16 chars; empty
    // is allowed). The result parses through d64_image and serves over the
    // synthetic 1541 exactly like a real disk, so a bare .prg can be loaded the
    // device-accurate way -- the KERNAL pulls it over IEC at native pace -- rather
    // than injected straight into RAM.
    //
    // Returns an empty vector if `prg` is empty or larger than the 664 free data
    // blocks a 35-track disk holds (664 * 254 bytes).
    [[nodiscard]] std::vector<std::uint8_t> make_prg_disk(std::span<const std::uint8_t> prg,
                                                          std::span<const std::uint8_t> name = {});

} // namespace mnemos::chips::storage::c1541
