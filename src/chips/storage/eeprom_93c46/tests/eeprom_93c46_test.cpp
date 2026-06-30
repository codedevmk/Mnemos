#include "eeprom_93c46.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::storage::eeprom_93c46;

    // Drives an eeprom_93c46 the way a cartridge port would: CS/CLK/DI bit-banged
    // per the Microwire convention (DI sampled on the CLK low->high edge while CS
    // is high). Opcodes are the 8-bit command byte (2-bit command + 6-bit address).
    struct mw_host {
        eeprom_93c46& e;

        void clk_bit(bool di) {
            e.update(true, false, di); // CS high, CLK low, DI presented
            e.update(true, true, di);  // CLK rising edge latches DI
        }
        bool clk_out() {
            e.update(true, false, false);
            e.update(true, true, false); // rising edge drives the next DO bit
            return e.data_out();
        }
        void send(unsigned value, int n) {
            for (int i = n - 1; i >= 0; --i) {
                clk_bit(((value >> i) & 1U) != 0U);
            }
        }
        void start_cmd(unsigned opcode8) {
            e.update(true, false, false); // raise CS
            clk_bit(true);                // START bit
            send(opcode8, 8);             // command + word address
        }
        void cs_low() { e.update(false, false, false); }

        void cmd(unsigned opcode8) {
            start_cmd(opcode8);
            cs_low();
        }
        void write_word(std::uint8_t addr, std::uint16_t data) {
            start_cmd(0x40U | (addr & 0x3FU)); // WRITE
            send(data, 16);
            cs_low();
        }
        std::uint16_t read_word(std::uint8_t addr) {
            start_cmd(0x80U | (addr & 0x3FU)); // READ
            std::uint16_t v = 0;
            for (int i = 0; i < 16; ++i) {
                v = static_cast<std::uint16_t>((v << 1U) | (clk_out() ? 1U : 0U));
            }
            cs_low();
            return v;
        }
        void ewen() { cmd(0x30U); } // 00 11 .... : write-enable
        void ewds() { cmd(0x00U); } // 00 00 .... : write-disable
        void erase(std::uint8_t addr) { cmd(0xC0U | (addr & 0x3FU)); }
        void erase_all() { cmd(0x20U); } // 00 10 ....
        void write_all(std::uint16_t data) {
            start_cmd(0x10U); // 00 01 .... : WRITE ALL
            send(data, 16);
            cs_low();
        }
    };
} // namespace

TEST_CASE("eeprom_93c46 powers on erased (128 bytes of 0xFF), DO released") {
    eeprom_93c46 e;
    REQUIRE(e.bytes().size() == 128U);
    CHECK(
        std::all_of(e.bytes().begin(), e.bytes().end(), [](std::uint8_t b) { return b == 0xFFU; }));
    CHECK(e.data_out()); // idle DO reads high
}

TEST_CASE("eeprom_93c46 round-trips a word after write-enable") {
    eeprom_93c46 e;
    mw_host h{e};
    h.ewen();
    h.write_word(5, 0x1234U);
    CHECK(h.read_word(5) == 0x1234U);

    // Stored little-endian: word 5 occupies bytes [10] (low) / [11] (high).
    CHECK(e.bytes()[10] == 0x34U);
    CHECK(e.bytes()[11] == 0x12U);
}

TEST_CASE("eeprom_93c46 ignores writes until write-enabled") {
    eeprom_93c46 e;
    mw_host h{e};
    h.write_word(6, 0xBEEFU); // no prior EWEN -> protected
    CHECK(h.read_word(6) == 0xFFFFU);

    h.ewen();
    h.write_word(6, 0xBEEFU);
    CHECK(h.read_word(6) == 0xBEEFU);

    h.ewds(); // re-arm protection
    h.write_word(6, 0x0000U);
    CHECK(h.read_word(6) == 0xBEEFU); // unchanged
}

TEST_CASE("eeprom_93c46 erase sets a word back to 0xFFFF") {
    eeprom_93c46 e;
    mw_host h{e};
    h.ewen();
    h.write_word(3, 0xAAAAU);
    REQUIRE(h.read_word(3) == 0xAAAAU);
    h.erase(3);
    CHECK(h.read_word(3) == 0xFFFFU);
}

TEST_CASE("eeprom_93c46 write-all and erase-all cover every word") {
    eeprom_93c46 e;
    mw_host h{e};
    h.ewen();
    h.write_all(0x5A5AU);
    CHECK(h.read_word(0) == 0x5A5AU);
    CHECK(h.read_word(63) == 0x5A5AU);

    h.erase_all();
    CHECK(h.read_word(0) == 0xFFFFU);
    CHECK(h.read_word(63) == 0xFFFFU);
}

TEST_CASE("eeprom_93c46 reads sequentially across words (93C46B)") {
    eeprom_93c46 e;
    mw_host h{e};
    h.ewen();
    h.write_word(10, 0x1111U);
    h.write_word(11, 0x2222U);

    // A single READ command that keeps clocking rolls into the next word.
    h.start_cmd(0x80U | 10U);
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    for (int i = 0; i < 16; ++i) {
        first = static_cast<std::uint16_t>((first << 1U) | (h.clk_out() ? 1U : 0U));
    }
    for (int i = 0; i < 16; ++i) {
        second = static_cast<std::uint16_t>((second << 1U) | (h.clk_out() ? 1U : 0U));
    }
    h.cs_low();
    CHECK(first == 0x1111U);
    CHECK(second == 0x2222U);
}

TEST_CASE("eeprom_93c46 reset returns to standby with DO released") {
    eeprom_93c46 e;
    mw_host h{e};
    h.ewen();
    h.start_cmd(0x80U | 0U); // mid-read
    (void)h.clk_out();
    e.reset();
    CHECK(e.data_out());
    CHECK_FALSE(e.chip_select());
}

TEST_CASE("eeprom_93c46 set_organization restraps a byte8 device to 16-bit (CP1B1F board)") {
    // The CP1B1F board (e.g. pang3) straps ORG high (16-bit: 64 x 16, an 8-bit
    // opcode of 2 command + 6 address bits). A host that constructs the device
    // for the 8-bit QSound strap and only later learns it is on a CP1B1F board
    // restraps via set_organization; the mw_host's 8-bit-opcode / 16-bit-word
    // protocol must then round-trip (it would misalign by one bit in 8-bit org).
    eeprom_93c46 e{eeprom_93c46::organization::byte8};
    e.set_organization(eeprom_93c46::organization::word16);
    mw_host h{e};
    h.ewen();
    h.write_word(7, 0xC0DEU);
    CHECK(h.read_word(7) == 0xC0DEU);
    CHECK(e.bytes()[14] == 0xDEU); // word 7 low byte
    CHECK(e.bytes()[15] == 0xC0U); // word 7 high byte
}

TEST_CASE("eeprom_93c46 save/load resumes an in-flight serial read") {
    eeprom_93c46 e;
    mw_host h{e};
    h.ewen();
    h.write_word(12, 0xA55AU);

    h.start_cmd(0x80U | 12U);
    std::uint16_t prefix = 0U;
    for (int i = 0; i < 5; ++i) {
        prefix = static_cast<std::uint16_t>((prefix << 1U) | (h.clk_out() ? 1U : 0U));
    }

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    e.save_state(writer);

    eeprom_93c46 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    mw_host h2{restored};
    std::uint16_t suffix = 0U;
    for (int i = 0; i < 11; ++i) {
        suffix = static_cast<std::uint16_t>((suffix << 1U) | (h2.clk_out() ? 1U : 0U));
    }
    h2.cs_low();

    CHECK(static_cast<std::uint16_t>((prefix << 11U) | suffix) == 0xA55AU);
}
