// NES Famicom Disk System manifest tests: image detection/parsing plus the
// RP2C33 RAM adapter, drive streaming, and multi-side swapping behavior.

#include "fds.hpp"
#include "nes_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    using namespace mnemos::manifests::nes;

    // A 16 KiB-PRG / 8 KiB-CHR NROM fixture used only as an iNES negative case for
    // FDS image detection. Keep this local so nes_system_test.cpp retains ownership
    // of the broader cartridge fixtures.
    std::vector<std::uint8_t> make_synthetic_nrom() {
        std::vector<std::uint8_t> rom(16U + 0x4000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U; // 1 x 16 KiB PRG
        rom[5] = 1U; // 1 x 8 KiB CHR

        const std::size_t prg = 16U; // PRG starts right after the 16-byte header
        const std::array<std::uint8_t, 8> code = {
            0xA9U, 0x42U,        // LDA #$42
            0x8DU, 0x00U, 0x02U, // STA $0200
            0x4CU, 0x05U, 0x80U, // JMP $8005 (self-loop)
        };
        for (std::size_t i = 0; i < code.size(); ++i) {
            rom[prg + i] = code[i];
        }
        rom[prg + 0x3FFCU] = 0x00U; // reset vector low
        rom[prg + 0x3FFDU] = 0x80U; // reset vector high -> $8000
        return rom;
    }

    // A minimal one-side Famicom Disk System image (.fds): the 16-byte header, then a
    // disk-info block ($01 + "*NINTENDO-HVC*") and a file-amount block ($02, 0 files),
    // padded to the 65500-byte side. Enough to exercise the parser + disk drive.
    std::vector<std::uint8_t> make_synthetic_fds() {
        std::vector<std::uint8_t> rom(16U + 65500U, 0x00U);
        rom[0] = 'F';
        rom[1] = 'D';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U; // one disk side
        const std::size_t s = 16U;
        rom[s + 0U] = 0x01U; // disk-info block code
        const char* hvc = "*NINTENDO-HVC*";
        for (std::size_t i = 0; i < 14U; ++i) {
            rom[s + 1U + i] = static_cast<std::uint8_t>(hvc[i]);
        }
        rom[s + 56U] = 0x02U; // file-amount block code (block 1 is 56 bytes)
        rom[s + 57U] = 0x00U; // zero files
        return rom;
    }

    // A two-side FDS image: each side is a disk-info block ($01 + "*NINTENDO-HVC*")
    // whose game-name byte (offset $10) is 'A' on side 0 and 'B' on side 1, so a test
    // can tell which side the drive is serving, plus a file-amount block (0 files).
    std::vector<std::uint8_t> make_synthetic_fds_2side() {
        std::vector<std::uint8_t> rom(16U + 2U * 65500U, 0x00U);
        rom[0] = 'F';
        rom[1] = 'D';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 2U; // two sides
        const char* hvc = "*NINTENDO-HVC*";
        for (std::size_t side = 0; side < 2U; ++side) {
            const std::size_t s = 16U + side * 65500U;
            rom[s + 0U] = 0x01U;
            for (std::size_t i = 0; i < 14U; ++i) {
                rom[s + 1U + i] = static_cast<std::uint8_t>(hvc[i]);
            }
            rom[s + 0x10U] = static_cast<std::uint8_t>('A' + side); // per-side marker
            rom[s + 56U] = 0x02U;
            rom[s + 57U] = 0x00U;
        }
        return rom;
    }

    // A stand-in 8 KiB FDS BIOS: just enough to assemble (a reset vector to $E000 and
    // a self-loop). The real DISKSYS.ROM is supplied via MNEMOS_FDS_BIOS for golden runs.
    std::vector<std::uint8_t> make_dummy_fds_bios() {
        std::vector<std::uint8_t> bios(0x2000U, 0x00U);
        bios[0x0000U] = 0x4CU; // JMP $E000
        bios[0x0001U] = 0x00U;
        bios[0x0002U] = 0xE0U;
        bios[0x1FFCU] = 0x00U; // reset vector -> $E000
        bios[0x1FFDU] = 0xE0U;
        return bios;
    }

} // namespace

TEST_CASE("looks_like_fds distinguishes FDS disks from iNES carts", "[manifests][nes][fds]") {
    CHECK(looks_like_fds(make_synthetic_fds())); // headered "FDS\x1A"

    std::vector<std::uint8_t> headerless(k_fds_side_size, 0x00U);
    headerless[0] = 0x01U;
    CHECK(looks_like_fds(headerless)); // headerless single side

    CHECK_FALSE(looks_like_fds(make_synthetic_nrom()));               // an iNES cart
    CHECK_FALSE(looks_like_fds(std::vector<std::uint8_t>(100U, 0U))); // neither
}

TEST_CASE("parse_fds_sides strips the header and returns whole sides", "[manifests][nes][fds]") {
    const auto sides = parse_fds_sides(make_synthetic_fds());
    REQUIRE(sides.size() == k_fds_side_size); // one side, 16-byte header removed
    CHECK(sides[0] == 0x01U);                 // disk-info block is first
    CHECK(sides[1] == static_cast<std::uint8_t>('*'));
}

TEST_CASE("FDS image assembles the RP2C33 RAM adapter (RAM + BIOS + drive)",
          "[manifests][nes][fds]") {
    nes_config cfg;
    cfg.fds_bios = make_dummy_fds_bios();
    auto sys = assemble_nes(make_synthetic_fds(), cfg);
    REQUIRE(sys->is_fds);

    // $E000-$FFFF is the BIOS (its reset vector resolves); $6000-$DFFF is 32 KiB RAM.
    CHECK(sys->bus.read8(0xFFFCU) == 0x00U);
    CHECK(sys->bus.read8(0xFFFDU) == 0xE0U);
    sys->bus.write8(0x6000U, 0x5AU);
    sys->bus.write8(0xDFFFU, 0xA5U);
    CHECK(sys->bus.read8(0x6000U) == 0x5AU);
    CHECK(sys->bus.read8(0xDFFFU) == 0xA5U);

    // $4032 reports the disk inserted + ready (bits 0/1 clear).
    CHECK((sys->bus.read8(0x4032U) & 0x03U) == 0x00U);
}

TEST_CASE("FDS disk drive streams block bytes with synthesized CRC gaps", "[manifests][nes][fds]") {
    nes_config cfg;
    cfg.fds_bios = make_dummy_fds_bios();
    auto sys = assemble_nes(make_synthetic_fds(), cfg);

    sys->bus.write8(0x4023U, 0x83U); // enable the disk registers
    sys->bus.write8(0x4025U, 0x2DU); // motor on, transfer running, read mode

    // Pull bytes via the byte-ready ($4030 bit 7) / read-data ($4031) handshake,
    // clocking the drive (the board normally does this once per scanline).
    const auto next_byte = [&]() -> int {
        for (int i = 0; i < 20000; ++i) {
            if ((sys->bus.read8(0x4030U) & 0x80U) != 0U) {
                return sys->bus.read8(0x4031U);
            }
            sys->mapper->clock_cpu_timer(114U);
        }
        return -1;
    };

    std::array<int, 60> b{};
    for (int& v : b) {
        v = next_byte();
    }
    CHECK(b[0] == 0x01);                  // block 1 (disk info) ID
    CHECK(b[1] == static_cast<int>('*')); // "*NINTENDO-HVC*"
    CHECK(b[56] == 0x00);                 // synthesized CRC byte after block 1's 56 bytes
    CHECK(b[57] == 0x00);                 // (the raw .fds has no CRC; the drive supplies it)
    CHECK(b[58] == 0x02);                 // block 2 (file amount) ID, correctly aligned
    CHECK(b[59] == 0x00);                 // zero files
}

TEST_CASE("FDS multi-side disk swapping flips the served side", "[manifests][nes][fds]") {
    nes_config cfg;
    cfg.fds_bios = make_dummy_fds_bios();
    auto sys = assemble_nes(make_synthetic_fds_2side(), cfg);
    REQUIRE(sys->is_fds);
    REQUIRE(sys->mapper->disk_side_count() == 2U);
    CHECK(sys->mapper->current_disk_side() == 0U);

    // Drive the disk from its start and return the disk-info block's game-name byte
    // (offset $10 = the 17th byte), which differs per side.
    const auto read_side_marker = [&]() -> int {
        sys->bus.write8(0x4025U, 0x2EU); // hold/rewind to the side's start
        sys->bus.write8(0x4023U, 0x83U); // enable the disk registers
        sys->bus.write8(0x4025U, 0x2DU); // motor on, transfer running, read
        int byte = -1;
        for (int b = 0; b < 17; ++b) {
            byte = -1;
            for (int i = 0; i < 40000 && byte < 0; ++i) {
                if ((sys->bus.read8(0x4030U) & 0x80U) != 0U) {
                    byte = sys->bus.read8(0x4031U);
                } else {
                    sys->mapper->clock_cpu_timer(114U);
                }
            }
        }
        return byte; // the game-name byte (block offset $10)
    };

    CHECK(read_side_marker() == static_cast<int>('A')); // side 0

    sys->mapper->insert_disk_side(1U);
    CHECK(sys->mapper->current_disk_side() == 1U);
    CHECK((sys->bus.read8(0x4032U) & 0x01U) != 0U);     // mid-swap: the disk reports removed
    CHECK(read_side_marker() == static_cast<int>('B')); // side 1 after the flip
}
