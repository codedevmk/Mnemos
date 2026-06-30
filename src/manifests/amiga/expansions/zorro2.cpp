#include "expansions/zorro2.hpp"

#include "amiga_memory_sizes.hpp"

namespace mnemos::manifests::amiga {

    namespace {
        [[nodiscard]] std::uint8_t zorro2_size_code(std::size_t size) noexcept {
            if (size >= amiga_size_8m) {
                return 0U;
            }
            if (size > amiga_size_4m) {
                return 0U;
            }
            if (size > amiga_size_2m) {
                return 7U;
            }
            if (size > amiga_size_1m) {
                return 6U;
            }
            if (size > amiga_size_512k) {
                return 5U;
            }
            return 4U;
        }

        [[nodiscard]] std::uint8_t zorro2_logical_config_byte(
            const zorro2_expansion_board& board, std::uint8_t logical_offset) noexcept {
            constexpr std::uint8_t ert_newboard = 0xC0U;
            constexpr std::uint8_t ert_memlist = 0x20U;
            constexpr std::uint8_t erff_memspace = 0x80U;

            switch (logical_offset) {
            case 0x00U:
                return static_cast<std::uint8_t>(
                    ert_newboard | (board.memory ? ert_memlist : 0U) |
                    zorro2_size_code(board.memory_size));
            case 0x01U:
                return board.product;
            case 0x02U:
                return board.memory ? erff_memspace : 0U;
            case 0x04U:
                return static_cast<std::uint8_t>(board.manufacturer >> 8U);
            case 0x05U:
                return static_cast<std::uint8_t>(board.manufacturer);
            case 0x06U:
                return static_cast<std::uint8_t>(board.serial >> 24U);
            case 0x07U:
                return static_cast<std::uint8_t>(board.serial >> 16U);
            case 0x08U:
                return static_cast<std::uint8_t>(board.serial >> 8U);
            case 0x09U:
                return static_cast<std::uint8_t>(board.serial);
            case 0x0AU:
            case 0x0BU:
                return 0U;
            default:
                return 0U;
            }
        }

        [[nodiscard]] std::uint8_t zorro2_config_storage_byte(
            const zorro2_expansion_board& board, std::uint8_t logical_offset) noexcept {
            const std::uint8_t value = zorro2_logical_config_byte(board, logical_offset);
            return logical_offset == 0U ? value : static_cast<std::uint8_t>(~value);
        }

        [[nodiscard]] std::uint8_t repeat_nibble(std::uint8_t nibble) noexcept {
            nibble = static_cast<std::uint8_t>(nibble & 0x0FU);
            return static_cast<std::uint8_t>((nibble << 4U) | nibble);
        }
    } // namespace

    std::uint8_t zorro2_autoconfig_read(const zorro2_expansion_board& board,
                                        std::uint32_t autoconfig_base,
                                        std::uint32_t address) noexcept {
        const std::uint32_t physical = (address - autoconfig_base) & 0xFFFFU;
        const auto logical_offset = static_cast<std::uint8_t>((physical >> 2U) & 0x3FU);
        const std::uint8_t value = zorro2_config_storage_byte(board, logical_offset);
        const bool high_nibble = (physical & 0x02U) == 0U;
        const std::uint8_t nibble = high_nibble ? static_cast<std::uint8_t>(value >> 4U)
                                                : static_cast<std::uint8_t>(value & 0x0FU);
        return repeat_nibble(nibble);
    }

    std::uint8_t zorro2_write_nibble(std::uint8_t value) noexcept {
        return (value & 0xF0U) != 0U ? static_cast<std::uint8_t>(value >> 4U)
                                     : static_cast<std::uint8_t>(value & 0x0FU);
    }

} // namespace mnemos::manifests::amiga
