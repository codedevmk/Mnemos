#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::chips::storage {

    // Serial I2C EEPROM (Microchip 24Cxx family), as used for cartridge battery
    // saves on several consoles. A combinational slave driven entirely by its two
    // lines -- SCL (clock) and SDA (bidirectional data) -- with no clock of its own.
    //
    // The host drives both lines via update(scl, sda); the device latches on the
    // SCL/SDA edges (START = SDA falling while SCL high, STOP = SDA rising while
    // SCL high, data sampled on SCL rising) and answers reads on sda(). The pin
    // mapping (which bus address/bit carries each line) is the cartridge mapper's
    // job, not the device's.
    //
    // Capacity selects the addressing mode the way the real parts work:
    //   <= 256 B (24C01/02): one word-address byte.
    //   512 B - 2 KiB (24C04/08/16): one word-address byte plus the low bits of the
    //     control byte as the high address bits (a single device, block-selected).
    //   >= 4 KiB (24C32/64/65): two word-address bytes.
    class eeprom_i2c {
      public:
        explicit eeprom_i2c(std::size_t size_bytes);

        // Backing store (erased state is 0xFF), exposed for .srm persistence.
        [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return store_; }
        [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return store_; }

        // Host drives the two lines to their new levels.
        void update(bool scl, bool sda) noexcept;
        // Device's SDA output: true = line released (pulled high), false = held low.
        [[nodiscard]] bool sda() const noexcept;

        void reset() noexcept;

      private:
        // A received byte is complete: act on it per the current stage (control ->
        // R/W + high address bits, then word address(es), then stored data) and
        // advance to the next stage.
        void on_byte(std::uint8_t byte) noexcept;

        std::vector<std::uint8_t> store_;
        std::uint32_t addr_mask_{}; // store_.size() - 1 (sizes are powers of two)
        int word_addr_bytes_{1};    // 1 (<=2 KiB) or 2 (>=4 KiB) word-address bytes
        int block_bits_{0};         // high address bits carried in the control byte

        // I2C line + transfer state.
        bool prev_scl_{true};
        bool prev_sda_{true};
        bool sda_out_{true}; // device-driven SDA (true = released high)

        enum class stage { idle, control, word_hi, word_lo, write_data, read_data };
        stage stage_{stage::idle};
        int bit_count_{0};           // bits clocked in the current byte (0..8, 8 = ACK)
        std::uint8_t shift_in_{};    // byte being received
        std::uint8_t shift_out_{};   // byte being transmitted on reads
        std::uint32_t addr_{};       // word-address pointer into store_
        std::uint32_t block_high_{}; // address bits latched from the control byte
        bool reading_{};             // R/W bit of the control byte (true = read)
        bool transmitting_{};        // currently clocking a byte out to the master
        bool master_ack_{};          // master's ACK after a read byte (true = continue)
    };

} // namespace mnemos::chips::storage
