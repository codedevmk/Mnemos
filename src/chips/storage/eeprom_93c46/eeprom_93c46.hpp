#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::storage {

    // Serial Microwire EEPROM (93C46): 64 x 16-bit words = 128 bytes of cartridge
    // save storage, organised 16-bit (the ORG pin tied high). A combinational slave
    // driven by three input lines -- CS (chip select), CLK (clock), DI (serial data
    // in) -- with one serial data-out line, DO. It has no clock of its own.
    //
    // The host drives the lines via update(cs, clk, di); the device latches DI on
    // each CLK rising edge while CS is high, and answers reads on data_out(). A
    // command is a START bit (DI high) followed by an 8-bit opcode (2-bit command +
    // 6-bit word address); WRITE/WRAL then clock in 16 data bits, READ clocks 16 out
    // MSB-first (auto-incrementing for sequential reads). Writes/erases require a
    // prior EWEN (write-enable) and are otherwise ignored. The pin mapping (which bus
    // address/bit carries each line) is the cartridge mapper's job, not the device's.
    class eeprom_93c46 {
      public:
        static constexpr std::size_t size_bytes = 128; // 64 x 16-bit words

        eeprom_93c46() { store_.fill(0xFFU); }

        // Backing store (erased state is 0xFF), exposed for .srm persistence. Words
        // are little-endian: word w occupies bytes [w*2] (low) and [w*2+1] (high).
        [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return store_; }
        [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return store_; }

        // Host drives the three input lines to their new levels. DI is sampled on a
        // CLK low->high edge while CS is high; a CS high->low edge ends the command.
        void update(bool cs, bool clk, bool di) noexcept;

        // Device serial data-out (DO). High (released) in standby; during a READ it
        // streams the addressed word MSB-first after a leading dummy 0.
        [[nodiscard]] bool data_out() const noexcept { return data_out_; }
        // Latched chip-select level (the cartridge port echoes it on read-back).
        [[nodiscard]] bool chip_select() const noexcept { return cs_; }

        void reset() noexcept;

      private:
        enum class stage : std::uint8_t { standby, wait_start, get_opcode, write_word, read_word };

        [[nodiscard]] std::uint16_t word_at(std::uint8_t addr) const noexcept {
            const std::size_t off = static_cast<std::size_t>(addr & 0x3FU) << 1U;
            return static_cast<std::uint16_t>(store_[off] | (store_[off + 1U] << 8U));
        }
        void set_word(std::uint8_t addr, std::uint16_t value) noexcept {
            const std::size_t off = static_cast<std::size_t>(addr & 0x3FU) << 1U;
            store_[off] = static_cast<std::uint8_t>(value & 0xFFU);
            store_[off + 1U] = static_cast<std::uint8_t>(value >> 8U);
        }

        // Act on the fully-clocked 8-bit opcode (command + word address).
        void decode_opcode() noexcept;

        std::array<std::uint8_t, size_bytes> store_{};

        stage stage_{stage::wait_start};
        bool cs_{};              // latched CS line
        bool clk_{};             // latched CLK line (for edge detection)
        bool data_out_{true};    // DO line (true = released high)
        bool write_enable_{};    // set by EWEN, cleared by EWDS; gates writes/erases
        std::uint8_t cycles_{};  // bits clocked in the current phase
        std::uint8_t opcode_{};  // 2-bit command + 6-bit word address
        std::uint16_t buffer_{}; // 16-bit data shift register (in for WRITE, out for READ)
    };

} // namespace mnemos::chips::storage
