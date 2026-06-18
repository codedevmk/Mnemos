#include "eeprom_i2c.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::storage::eeprom_i2c;

    // Drives an eeprom_i2c the way a cartridge mapper would: one SCL/SDA line pair,
    // bit-banged per the I2C convention (data set while SCL low, sampled on SCL
    // high; START = SDA falling and STOP = SDA rising while SCL high).
    struct i2c_host {
        eeprom_i2c& e;

        void set(bool scl, bool sda) { e.update(scl, sda); }

        void start() {
            set(true, true);
            set(true, false); // SDA high->low while SCL high
            set(false, false);
        }
        void stop() {
            set(false, false);
            set(true, false);
            set(true, true); // SDA low->high while SCL high
        }
        // Clock out 8 bits MSB-first, then release SDA and clock the 9th; returns
        // true if the device pulled SDA low to acknowledge.
        bool write_byte(std::uint8_t b) {
            for (int i = 7; i >= 0; --i) {
                const bool bit = ((b >> i) & 1U) != 0U;
                set(false, bit);
                set(true, bit);
            }
            set(false, true);
            set(true, true);
            const bool ack = !e.sda(); // device holds SDA low to ACK
            set(false, true);
            return ack;
        }
        // Clock in 8 bits the device drives, then send ACK (continue) or NAK (end).
        std::uint8_t read_byte(bool ack) {
            std::uint8_t v = 0;
            for (int i = 7; i >= 0; --i) {
                set(false, true); // release SDA so the device can drive it
                set(true, true);
                v = static_cast<std::uint8_t>((v << 1) | (e.sda() ? 1U : 0U));
            }
            set(false, !ack); // ACK = master pulls SDA low
            set(true, !ack);
            set(false, !ack);
            return v;
        }
    };
} // namespace

TEST_CASE("eeprom_i2c powers on erased and exposes its capacity") {
    eeprom_i2c e(256);
    REQUIRE(e.bytes().size() == 256U);
    CHECK(
        std::all_of(e.bytes().begin(), e.bytes().end(), [](std::uint8_t b) { return b == 0xFFU; }));
    CHECK(e.sda()); // idle line is released (reads high)
}

TEST_CASE("eeprom_i2c stores a byte written over I2C (24C02, one address byte)") {
    eeprom_i2c e(256);
    i2c_host h{e};
    h.start();
    CHECK(h.write_byte(0xA0)); // control: 1010 + R/W=0 (write) -> device ACKs
    CHECK(h.write_byte(0x05)); // word address = $05
    CHECK(h.write_byte(0x42)); // data
    h.stop();
    CHECK(e.bytes()[0x05] == 0x42U);
}

TEST_CASE("eeprom_i2c reads back a stored byte (random read, 24C02)") {
    eeprom_i2c e(256);
    e.bytes()[0x05] = 0x42U; // preload the cell
    i2c_host h{e};
    h.start();
    CHECK(h.write_byte(0xA0));                  // write control: set the address pointer...
    CHECK(h.write_byte(0x05));                  // ...word address $05
    h.start();                                  // repeated START
    CHECK(h.write_byte(0xA1));                  // read control
    CHECK(h.read_byte(/*ack=*/false) == 0x42U); // read one byte, NAK to end
    h.stop();
}

TEST_CASE("eeprom_i2c sequential read auto-increments the address pointer") {
    eeprom_i2c e(256);
    e.bytes()[0x10] = 0x11U;
    e.bytes()[0x11] = 0x22U;
    e.bytes()[0x12] = 0x33U;
    i2c_host h{e};
    h.start();
    CHECK(h.write_byte(0xA0));
    CHECK(h.write_byte(0x10));
    h.start();
    CHECK(h.write_byte(0xA1));
    CHECK(h.read_byte(/*ack=*/true) == 0x11U); // ACK -> keep reading
    CHECK(h.read_byte(/*ack=*/true) == 0x22U);
    CHECK(h.read_byte(/*ack=*/false) == 0x33U); // NAK -> stop
    h.stop();
}

TEST_CASE("eeprom_i2c page write stores consecutive bytes") {
    eeprom_i2c e(256);
    i2c_host h{e};
    h.start();
    CHECK(h.write_byte(0xA0));
    CHECK(h.write_byte(0x20)); // base address
    CHECK(h.write_byte(0xAA)); // -> $20
    CHECK(h.write_byte(0xBB)); // -> $21
    CHECK(h.write_byte(0xCC)); // -> $22
    h.stop();
    CHECK(e.bytes()[0x20] == 0xAAU);
    CHECK(e.bytes()[0x21] == 0xBBU);
    CHECK(e.bytes()[0x22] == 0xCCU);
}

TEST_CASE("eeprom_i2c 24C16 carries high address bits in the control byte") {
    eeprom_i2c e(2048); // 24C16: 11-bit address (8 word-address + 3 block bits)
    i2c_host h{e};
    // Write $77 to address $305 (block 3, offset $05): control 0xA0 | (3<<1) = 0xA6.
    h.start();
    CHECK(h.write_byte(0xA6));
    CHECK(h.write_byte(0x05));
    CHECK(h.write_byte(0x77));
    h.stop();
    CHECK(e.bytes()[0x305] == 0x77U);
    // Random-read it back: read control for block 3 = 0xA7.
    h.start();
    CHECK(h.write_byte(0xA6));
    CHECK(h.write_byte(0x05));
    h.start();
    CHECK(h.write_byte(0xA7));
    CHECK(h.read_byte(/*ack=*/false) == 0x77U);
    h.stop();
}

TEST_CASE("eeprom_i2c 24C65 uses two word-address bytes") {
    eeprom_i2c e(8192); // 24C65: 13-bit address across two word-address bytes
    i2c_host h{e};
    h.start();
    CHECK(h.write_byte(0xA0));
    CHECK(h.write_byte(0x12)); // address high
    CHECK(h.write_byte(0x34)); // address low -> $1234
    CHECK(h.write_byte(0x5AU));
    h.stop();
    CHECK(e.bytes()[0x1234] == 0x5AU);
    h.start();
    CHECK(h.write_byte(0xA0));
    CHECK(h.write_byte(0x12));
    CHECK(h.write_byte(0x34));
    h.start();
    CHECK(h.write_byte(0xA1));
    CHECK(h.read_byte(/*ack=*/false) == 0x5AU);
    h.stop();
}

TEST_CASE("eeprom_i2c reset returns the bus to idle without erasing the store") {
    eeprom_i2c e(256);
    e.bytes()[0x00] = 0x5AU;
    i2c_host h{e};
    h.start();
    CHECK(h.write_byte(0xA0)); // mid-transaction
    e.reset();
    CHECK(e.sda());                  // line released
    CHECK(e.bytes()[0x00] == 0x5AU); // contents survive a reset (battery-backed)
}

TEST_CASE("eeprom_i2c save_state/load_state captures an in-flight transaction") {
    // Drive a device into the middle of a write (control + word address latched,
    // mid-data), so the saved state carries the stage/address pointer -- not just
    // the backing store, which already rides a memory chunk in the machine save.
    eeprom_i2c live(256);
    i2c_host hlive{live};
    hlive.start();
    CHECK(hlive.write_byte(0xA0)); // control: write
    CHECK(hlive.write_byte(0x05)); // word address $05 -> stage=write_data, addr=$05

    std::vector<std::uint8_t> buf;
    mnemos::chips::state_writer writer(buf);
    live.save_state(writer);

    // Restore into a fresh device of the same capacity whose store has diverged.
    eeprom_i2c loaded(256);
    loaded.bytes()[0x05] = 0x11U;
    mnemos::chips::state_reader reader(buf);
    loaded.load_state(reader);
    REQUIRE(reader.ok());

    // Continue the identical remaining transaction on both: the next data byte must
    // land at the latched address ($05) on the restored device just as on the live
    // one -- proving the in-flight stage + address pointer survived the round-trip.
    i2c_host hloaded{loaded};
    CHECK(hlive.write_byte(0xCD));
    CHECK(hloaded.write_byte(0xCD));
    hlive.stop();
    hloaded.stop();
    CHECK(live.bytes()[0x05] == 0xCDU);
    CHECK(loaded.bytes()[0x05] == live.bytes()[0x05]);
}

TEST_CASE("eeprom_i2c load_state rejects a truncated stream") {
    eeprom_i2c e(256);
    std::vector<std::uint8_t> buf;
    mnemos::chips::state_writer writer(buf);
    e.save_state(writer);
    REQUIRE(buf.size() > 8U);

    eeprom_i2c loaded(256);
    const std::vector<std::uint8_t> truncated(buf.begin(), buf.begin() + 4); // store cut short
    mnemos::chips::state_reader reader(truncated);
    loaded.load_state(reader);
    CHECK_FALSE(reader.ok());
}
