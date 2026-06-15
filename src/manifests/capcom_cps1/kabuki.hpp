#pragma once

#include <cstdint>
#include <span>

namespace mnemos::manifests::capcom_cps1 {

    // The "Kabuki" is a custom encrypted Z80 (Capcom's CPU package) used as the
    // sound CPU on QSound CPS1 boards. Its program ROM is stored encrypted; the
    // CPU decrypts each byte on the fly, and crucially decrypts a byte DIFFERENTLY
    // depending on whether it is fetched as an M1 opcode or read as data -- so the
    // decoded program is two streams over the same address space. The decryption
    // is a per-byte sequence of bit-swaps, rotates, and an XOR, parameterised by a
    // per-game key. Ported from the Emu reference (cps1.c).

    // The four key parameters for one game's Kabuki program.
    struct kabuki_keys final {
        std::uint32_t swap_key1{};
        std::uint32_t swap_key2{};
        std::uint16_t addr_key{};
        std::uint8_t xor_key{};
    };

    // The CPS1 QSound games whose sound CPU is Kabuki-encrypted. `slammast` is
    // shared by Saturday Night Slam Masters + Muscle Bomber Duo (same key).
    enum class kabuki_game : std::uint8_t { dino, wof, punisher, slammast };

    [[nodiscard]] kabuki_keys kabuki_keys_for(kabuki_game game) noexcept;

    // Decode an encrypted Kabuki program into its two streams: `opcode_out` (the
    // M1 instruction-fetch stream) and `data_out` (the operand/data-read stream).
    // All three spans must be the same length (the encrypted low region of the
    // sound ROM, typically 0x8000). A board then serves opcode_out through the bus
    // fetch_opcode8 path and data_out through read8 over the program window.
    void kabuki_decode(std::span<const std::uint8_t> encrypted, const kabuki_keys& keys,
                       std::span<std::uint8_t> opcode_out,
                       std::span<std::uint8_t> data_out) noexcept;

} // namespace mnemos::manifests::capcom_cps1
