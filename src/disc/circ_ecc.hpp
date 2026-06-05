#pragma once

// CD-ROM Mode-1 EDC + P/Q parity (CIRC) regeneration, per ECMA-130.
//
// A Mode-1 sector is 2352 bytes:
//   [0..11]      sync (00 FF*10 00)
//   [12..14]     header (MSF, BCD)
//   [15]         mode (0x01)
//   [16..2063]   user data (2048)
//   [2064..2067] EDC (CRC-32, little-endian)
//   [2068..2075] reserved (zero)
//   [2076..2247] P parity (172)
//   [2248..2351] Q parity (104)
//
// Used to reconstruct ECC for image formats that store only user data (ISO
// 2048) or when a CHD codec stripped it. Ported from the Emu reference
// (chips/circ_ecc); clean-room per ECMA-130. See NOTES.md.

#include <cstdint>
#include <span>

namespace mnemos::disc {

    // CD-ROM Mode-1 EDC: CRC-32 (reflected polynomial 0xD8018001, init 0, no
    // final XOR) over `data`. Returns the result in host byte order; the caller
    // stores it as four little-endian bytes at sector offset 2064.
    [[nodiscard]] std::uint32_t circ_ecc_edc(std::span<const std::uint8_t> data);

    // Regenerate the 288 ECC bytes (EDC + reserved + P + Q parity) of a Mode-1
    // sector. Bytes [0..2063] (sync + header + mode + user data) must already be
    // populated; on return bytes [2064..2351] hold the regenerated ECC.
    void circ_ecc_regen_sector(std::span<std::uint8_t, 2352> sector);

} // namespace mnemos::disc
