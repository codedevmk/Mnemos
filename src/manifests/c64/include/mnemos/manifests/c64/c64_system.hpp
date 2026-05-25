#pragma once

#include <mnemos/chips/audio/sid_6581.hpp>
#include <mnemos/chips/bus_controller/cia_6526.hpp>
#include <mnemos/chips/common/iec_bus.hpp>
#include <mnemos/chips/cpu/m6510.hpp>
#include <mnemos/chips/mapper/c64_cartridge.hpp>
#include <mnemos/chips/mapper/c64_pla.hpp>
#include <mnemos/chips/peripheral/modem.hpp>
#include <mnemos/chips/peripheral/reu.hpp>
#include <mnemos/chips/peripheral/rs232.hpp>
#include <mnemos/chips/storage/c1541/full_drive.hpp>
#include <mnemos/chips/storage/c1541/synthetic_drive.hpp>
#include <mnemos/chips/storage/datasette.hpp>
#include <mnemos/chips/video/vic_ii_6569.hpp>
#include <mnemos/manifests/c64/c64_input.hpp>
#include <mnemos/topology/bus.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::c64 {

    // Machine configuration knobs resolved at assembly time.
    struct c64_config final {
        enum class region : std::uint8_t { pal, ntsc };

        region video_region{region::pal};
        chips::audio::sid_6581::variant sid_variant{chips::audio::sid_6581::variant::mos_6581};
        bool dual_sid{false}; // a second SID at $D420 (stereo)
        bool reu{false};      // a RAM Expansion Unit at $DF00
        chips::peripheral::reu::model reu_model{chips::peripheral::reu::model::ram_512k};
        bool modem{false};               // an RS-232 userport modem (CIA2 PA2/PB0//FLAG)
        std::uint32_t rs232_baud{1200U}; // userport RS-232 baud (cycles/bit = phi2/baud)
    };

    // A fully wired Commodore 64: the six chips, 64K RAM + 1K colour RAM, the three
    // ROM images, and the main bus with PLA-driven banking. Heap-allocated and
    // never moved after assembly, because the bus regions hold spans into the
    // members and the banking predicates capture `this`.
    struct c64_system final {
        chips::cpu::m6510 cpu;
        chips::video::vic_ii_6569 vic;
        chips::audio::sid_6581 sid;
        chips::audio::sid_6581 sid2; // second SID at $D420, mapped only when dual_sid
        chips::bus_controller::cia_6526 cia1;
        chips::bus_controller::cia_6526 cia2;
        chips::mapper::c64_pla pla;
        chips::mapper::c64_cartridge cart; // expansion-port cartridge (empty by default)
        topology::bus bus{16U, topology::endianness::little};

        // IEC serial bus (C64 = device 0) and drive 8. Both drives share the bus as
        // device 8; the runner ticks exactly one (the protocol-level synthetic drive
        // by default, or the cycle-accurate full drive when a DOS ROM is supplied).
        chips::iec_bus iec;
        chips::storage::c1541::synthetic_drive drive8{8U};
        chips::storage::c1541::full_drive drive8_full{8U};

        // Keyboard matrix + joysticks, read through CIA1 ports A/B.
        c64_input input;

        // RAM Expansion Unit at $DF00, mapped only when c64_config::reu is set.
        chips::peripheral::reu reu_unit{chips::peripheral::reu::model::ram_128k};

        // Userport RS-232 modem: the bit-level UART bridges CIA2 PA2 (TXD) / PB0
        // (RXD) / /FLAG to the byte-level Hayes modem core. The modem's transport
        // (loopback / TCP) is installed by the frontend. Wired to CIA2 always; the
        // frontend only ticks them when c64_config::modem is set.
        chips::peripheral::rs232 rs232_unit;
        chips::peripheral::modem modem_unit;

        // Datasette: pulses CIA1 /FLAG, motor from $01 bit 5, sense on $01 bit 4.
        chips::storage::datasette tape;

        std::array<std::uint8_t, 0x10000> ram{};
        std::array<std::uint8_t, 0x0400> color_ram{}; // $D800-$DBFF
        std::vector<std::uint8_t> basic_rom;          // $A000-$BFFF (8K)
        std::vector<std::uint8_t> kernal_rom;         // $E000-$FFFF (8K)
        std::vector<std::uint8_t> chargen_rom;        // $D000-$DFFF (4K, VIC + CPU view)
    };

    // Assemble a banked C64 from the three ROM images (moved in). The PLA reads the
    // 6510 $01 port live on every access to decide which overlay (RAM / BASIC /
    // KERNAL / CHARGEN / I/O) is visible. ROM verification is the caller's job
    // (see ROMS.md); pass zero-filled images to exercise the banking without ROMs.
    [[nodiscard]] std::unique_ptr<c64_system> assemble_c64(std::vector<std::uint8_t> basic_rom,
                                                           std::vector<std::uint8_t> kernal_rom,
                                                           std::vector<std::uint8_t> chargen_rom,
                                                           const c64_config& config = {});

} // namespace mnemos::manifests::c64
