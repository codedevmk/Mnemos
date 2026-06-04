#pragma once

#include "bus.hpp"                // topology bus
#include "codemasters_mapper.hpp" // Codemasters mapper
#include "hicom_mapper.hpp"       // HiCom 188-in-1 Korean multicart mapper
#include "janggun_mapper.hpp"     // Janggun bit-reversed Korean mapper
#include "korean_mapper.hpp"      // standard Korean mapper
#include "korean_msx_mapper.hpp"  // Korean MSX 8 KiB mapper (+ Nemesis variant)
#include "multi_16k_mapper.hpp"   // 4-Pak All Action 16 KiB multicart mapper
#include "multi_4x8k_mapper.hpp"  // $2000 XOR 8 KiB multicart mapper
#include "peripheral.hpp"         // peripheral::device (controller ports)
#include "region.hpp"             // mnemos::video_region (shared)
#include "sms_mapper.hpp"         // Sega mapper
#include "sms_vdp.hpp"            // video
#include "sn76489.hpp"            // audio (PSG)
#include "z80.hpp"                // cpu

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::manifests::sms {

    struct sms_config final {
        // automatic picks Sega vs Codemasters from the cart's Codemasters
        // checksum at $7FE6/$7FE8; the others force one. The Korean families are
        // force-only: their carts carry no header signature, so a heuristic would
        // risk misdetecting (and breaking) Sega/Codemasters carts -- automatic
        // therefore never resolves to one of them. `korean_msx_nemesis` is the
        // MSX mapper with its boot-region remap (same chip, different variant);
        // `korean_hicom` and `korean_janggun` are the HiCom 188-in-1 multicart and
        // the bit-reversed Janggun cart; `korean_multi_4x8k` and `korean_multi_16k`
        // are the $2000-XOR and 4-Pak-All-Action multicarts.
        enum class mapper : std::uint8_t {
            automatic,
            sega,
            codemasters,
            korean,
            korean_msx,
            korean_msx_nemesis,
            korean_hicom,
            korean_janggun,
            korean_multi_4x8k,
            korean_multi_16k,
        };

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
        // All mappers are members; assembly wires exactly one into the bus based
        // on the cart. The Sega mapper, the HiCom multicart, and the Janggun cart
        // page through a register overlay above the cartridge window ($FFFC-$FFFF,
        // $FFFF, and $FFFE-$FFFF); the Codemasters and the standard/MSX Korean
        // mappers and both Multi multicarts bank through writes inside the
        // cartridge window (Janggun does both). The *_active flags record which
        // one is live (all false = Sega).
        chips::mapper::sms_mapper mapper;
        chips::mapper::codemasters_mapper codies;
        chips::mapper::korean_mapper korean;
        chips::mapper::korean_msx_mapper korean_msx;
        chips::mapper::hicom_mapper hicom;
        chips::mapper::janggun_mapper janggun;
        chips::mapper::multi_4x8k_mapper multi4x8k;
        chips::mapper::multi_16k_mapper multi16k;
        bool codemasters_active{};
        bool korean_active{};
        bool korean_msx_active{};
        bool korean_hicom_active{};
        bool korean_janggun_active{};
        bool korean_multi_4x8k_active{};
        bool korean_multi_16k_active{};
        topology::bus bus{16U, topology::endianness::little};

        std::array<std::uint8_t, 0x2000> ram{}; // 8 KiB, mirrored $C000 / $E000
        std::vector<std::uint8_t> rom;          // cartridge image (borrowed by the mapper)

        // Controller ports 1/2. Each holds the peripheral the host attached
        // (typically an SMS Control Pad; future: Light Phaser, Sports Pad,
        // Sega Mouse, ...). The $DC / $DD port reads multiplex bits from
        // both attached devices' 6-pin input bytes plus the I/O control
        // overlays at $3F.
        std::array<std::unique_ptr<peripheral::device>, 2> ports{};

        std::uint8_t io_ctrl{0xFFU}; // I/O control latch (port $3F): TH/TR direction + level
        bool reset_pressed{};

        void attach(int port, std::unique_ptr<peripheral::device> dev) noexcept {
            if (port >= 0 && port < 2) {
                ports[static_cast<std::size_t>(port)] = std::move(dev);
            }
        }

        [[nodiscard]] peripheral::device* port_device(int port) noexcept {
            return (port >= 0 && port < 2) ? ports[static_cast<std::size_t>(port)].get() : nullptr;
        }

        void set_reset_button(bool pressed) noexcept { reset_pressed = pressed; }
    };

    // True when a cartridge image carries a valid Codemasters checksum header
    // (the 16-bit word at $7FE6 plus the complement at $7FE8 sum to $10000),
    // which doubles as the Codemasters-mapper signature. Used to pick the mapper
    // when sms_config::cartridge_mapper is `automatic`. Shared by assemble_sms
    // and the manifest-path build_sms_runtime so both resolve carts identically.
    [[nodiscard]] bool detect_codemasters(std::span<const std::uint8_t> rom) noexcept;

    // Look up a cartridge's zlib CRC-32 in the Korean-mapper database; returns the
    // mapper it needs, or nullopt if the CRC is unknown. Catalogues the known Korean
    // carts whose mapper Mnemos implements (see THIRD-PARTY.md).
    [[nodiscard]] std::optional<sms_config::mapper>
    korean_mapper_for_crc(std::uint32_t crc) noexcept;

    // Auto-detect a Korean cartridge mapper from the ROM image: CRC-32 over the whole
    // image, looked up via korean_mapper_for_crc. nullopt when the image is unknown.
    [[nodiscard]] std::optional<sms_config::mapper>
    detect_korean_mapper(std::span<const std::uint8_t> rom) noexcept;

    // Resolve sms_config::cartridge_mapper to a concrete mapper kind (never
    // `automatic`): forced choices pass through; `automatic` auto-detects the Korean
    // families by ROM CRC (see detect_korean_mapper), then picks Codemasters vs Sega
    // from the checksum header. Shared by assemble_sms and build_sms_runtime so both
    // paths resolve carts identically.
    [[nodiscard]] sms_config::mapper resolve_mapper(const sms_config& config,
                                                    std::span<const std::uint8_t> rom) noexcept;

    // Assemble a bootable SMS from a cartridge image (moved in). The mapper banks the
    // image; reads of $C000-$FFFF hit work RAM (with the $FFFC-$FFFF mapper-register
    // overlay); Z80 IN/OUT routes to the VDP ($BE/$BF, $7E/$7F), the PSG ($40-$7F
    // writes), and the two joypads ($DC/$DD). The VDP /INT line is ORed into the Z80
    // IRQ. ROM verification is the caller's job; pass any image to exercise wiring.
    [[nodiscard]] std::unique_ptr<sms_system> assemble_sms(std::vector<std::uint8_t> rom,
                                                           const sms_config& config = {});

} // namespace mnemos::manifests::sms
