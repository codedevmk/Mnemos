#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::storage {

    // Serial Microwire EEPROM (93C46): 1024 bits of save storage, organised by the
    // ORG pin as 64 x 16-bit (ORG high -- the default, e.g. Game Gear) or 128 x 8-bit
    // (ORG low, e.g. the CPS1 QSound 3/4-player boards). A combinational slave driven
    // by three input lines -- CS (chip select), CLK (clock), DI (serial data in) --
    // with one serial data-out line, DO. It has no clock of its own.
    //
    // The host drives the lines via update(cs, clk, di); the device latches DI on
    // each CLK rising edge while CS is high, and answers reads on data_out(). A
    // command is a START bit (DI high) followed by a 2-bit command + word address (6
    // bits in 16-bit org, 7 bits in 8-bit org); WRITE/WRAL then clock in the data
    // word (16 or 8 bits), READ clocks it out MSB-first (auto-incrementing for
    // sequential reads). Writes/erases require a prior EWEN (write-enable) and are
    // otherwise ignored. The pin mapping (which bus address/bit carries each line) is
    // the host board's job, not the device's.
    class eeprom_93c46 {
      public:
        static constexpr std::size_t size_bytes = 128; // 1024 bits

        // Memory organisation, selected by the ORG pin: 16-bit (64 x 16, ORG high --
        // the default, e.g. Game Gear) or 8-bit (128 x 8, ORG low -- e.g. the CPS1
        // QSound 3/4-player boards). 8-bit org uses a 7-bit address + 8-bit data
        // words; 16-bit org a 6-bit address + 16-bit data words.
        enum class organization : std::uint8_t { word16, byte8 };

        explicit eeprom_93c46(organization org = organization::word16) : org_(org) {
            store_.fill(0xFFU);
        }

        // Reselect the ORG pin level before use. The pin is strapped on the board,
        // so a host that only learns the strap after construction (e.g. once its
        // board profile resolves) sets it here; this resets the serial state but
        // preserves the backing store. Same org family (93C46) either way.
        void set_organization(organization org) noexcept {
            org_ = org;
            reset();
        }

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

        // Org-derived widths: 8-bit org clocks a 7-bit address + 8-bit data; 16-bit
        // org a 6-bit address + 16-bit data (so the opcode after START is 2+addr
        // bits wide, and the cmd is the top 2 of the opcode).
        [[nodiscard]] unsigned addr_bits() const noexcept {
            return org_ == organization::byte8 ? 7U : 6U;
        }
        [[nodiscard]] unsigned data_bits() const noexcept {
            return org_ == organization::byte8 ? 8U : 16U;
        }
        [[nodiscard]] std::uint8_t word_count() const noexcept {
            return org_ == organization::byte8 ? 128U : 64U;
        }

        [[nodiscard]] std::uint16_t word_at(std::uint16_t addr) const noexcept {
            if (org_ == organization::byte8) {
                return store_[addr & 0x7FU];
            }
            const std::size_t off = static_cast<std::size_t>(addr & 0x3FU) << 1U;
            return static_cast<std::uint16_t>(store_[off] | (store_[off + 1U] << 8U));
        }
        void set_word(std::uint16_t addr, std::uint16_t value) noexcept {
            if (org_ == organization::byte8) {
                store_[addr & 0x7FU] = static_cast<std::uint8_t>(value & 0xFFU);
                return;
            }
            const std::size_t off = static_cast<std::size_t>(addr & 0x3FU) << 1U;
            store_[off] = static_cast<std::uint8_t>(value & 0xFFU);
            store_[off + 1U] = static_cast<std::uint8_t>(value >> 8U);
        }

        // Act on the fully-clocked opcode (command + word address; 8 or 9 bits).
        void decode_opcode() noexcept;

        organization org_{organization::word16};
        std::array<std::uint8_t, size_bytes> store_{};

        stage stage_{stage::wait_start};
        bool cs_{};              // latched CS line
        bool clk_{};             // latched CLK line (for edge detection)
        bool data_out_{true};    // DO line (true = released high)
        bool write_enable_{};    // set by EWEN, cleared by EWDS; gates writes/erases
        std::uint8_t cycles_{};  // bits clocked in the current phase
        std::uint16_t opcode_{}; // 2-bit command + 6/7-bit word address
        std::uint16_t buffer_{}; // data shift register (in for WRITE, out for READ)
    };

} // namespace mnemos::chips::storage
