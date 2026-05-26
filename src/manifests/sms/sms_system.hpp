#pragma once

#include "bus.hpp"                // topology bus
#include "codemasters_mapper.hpp" // Codemasters mapper
#include "region.hpp"             // mnemos::video_region (shared)
#include "sms_mapper.hpp"         // Sega mapper
#include "sms_vdp.hpp"            // video
#include "sn76489.hpp"            // audio (PSG)
#include "z80.hpp"                // cpu

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::sms {

    // Joypad buttons, one byte per controller; a set bit means pressed. The port
    // read inverts these to the SMS's active-low pin levels.
    namespace pad_button {
        inline constexpr std::uint8_t up = 0x01U;
        inline constexpr std::uint8_t down = 0x02U;
        inline constexpr std::uint8_t left = 0x04U;
        inline constexpr std::uint8_t right = 0x08U;
        inline constexpr std::uint8_t button_1 = 0x10U;
        inline constexpr std::uint8_t button_2 = 0x20U;
    } // namespace pad_button

    // Machine configuration resolved at assembly time. The video_region member
    // uses the project-wide mnemos::video_region enum (defined in
    // chips/shared/region.hpp) so the manifest, adapter, and frontend never
    // translate between equivalent local copies.
    struct sms_config final {
        // Cartridge mapper: `automatic` picks Sega vs Codemasters from the cart
        // header (the Codemasters checksum at $7FE6/$7FE8); the others force one.
        enum class mapper : std::uint8_t { automatic, sega, codemasters };

        mnemos::video_region video_region{mnemos::video_region::ntsc};
        mapper cartridge_mapper{mapper::automatic};
    };

    // A fully wired Sega Master System: the Z80, the VDP, the SN76489 PSG, the Sega
    // mapper, 8 KiB of work RAM, and the Z80 memory + I/O buses. Heap-allocated and
    // never moved after assembly, because the bus regions hold spans into the members
    // and the port/IRQ callbacks capture `this`.
    struct sms_system final {
        chips::cpu::z80 cpu;
        chips::video::sms_vdp vdp;
        chips::audio::sn76489 psg;
        // Both mappers are members; assembly wires exactly one into the bus based on
        // the cart (the Sega mapper pages through $FFFC-$FFFF, the Codemasters mapper
        // through ROM-space writes). codemasters_active records which one is live.
        chips::mapper::sms_mapper mapper;
        chips::mapper::codemasters_mapper codies;
        bool codemasters_active{};
        topology::bus bus{16U, topology::endianness::little};

        std::array<std::uint8_t, 0x2000> ram{}; // 8 KiB, mirrored $C000 / $E000
        std::vector<std::uint8_t> rom;          // cartridge image (borrowed by the mapper)

        // Controller state (active-high here; the port read inverts to active-low).
        std::array<std::uint8_t, 2> pad{};
        std::uint8_t io_ctrl{0xFFU}; // I/O control latch (port $3F): TH/TR direction + level
        bool reset_pressed{};

        void set_pad(int port, std::uint8_t buttons) noexcept {
            if (port >= 0 && port < 2) {
                pad[static_cast<std::size_t>(port)] = buttons;
            }
        }
        void set_reset_button(bool pressed) noexcept { reset_pressed = pressed; }
    };

    // Assemble a bootable SMS from a cartridge image (moved in). The mapper banks the
    // image; reads of $C000-$FFFF hit work RAM (with the $FFFC-$FFFF mapper-register
    // overlay); Z80 IN/OUT routes to the VDP ($BE/$BF, $7E/$7F), the PSG ($40-$7F
    // writes), and the two joypads ($DC/$DD). The VDP /INT line is ORed into the Z80
    // IRQ. ROM verification is the caller's job; pass any image to exercise wiring.
    [[nodiscard]] std::unique_ptr<sms_system> assemble_sms(std::vector<std::uint8_t> rom,
                                                           const sms_config& config = {});

} // namespace mnemos::manifests::sms
