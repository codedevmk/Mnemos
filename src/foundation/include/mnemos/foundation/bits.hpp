#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mnemos::foundation {

    enum class byte_order : std::uint8_t {
        little,
        big,
        mixed,
    };

    [[nodiscard]] constexpr byte_order native_byte_order() noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return byte_order::little;
        } else if constexpr (std::endian::native == std::endian::big) {
            return byte_order::big;
        } else {
            return byte_order::mixed;
        }
    }

    template <typename T>
    concept unsigned_integer = std::unsigned_integral<T> && !std::same_as<T, bool>;

    template <typename> inline constexpr bool dependent_false_v = false;

    template <unsigned_integer T>
    inline constexpr std::size_t bit_count_v = std::numeric_limits<T>::digits;

    template <unsigned_integer T> [[nodiscard]] constexpr int popcount(T value) noexcept {
        return std::popcount(value);
    }

    template <unsigned_integer T> [[nodiscard]] constexpr int countl_zero(T value) noexcept {
        return std::countl_zero(value);
    }

    template <unsigned_integer T> [[nodiscard]] constexpr T byte_swap(T value) noexcept {
        return std::byteswap(value);
    }

    template <unsigned_integer T> [[nodiscard]] constexpr T native_to_little(T value) noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return value;
        } else if constexpr (std::endian::native == std::endian::big) {
            return byte_swap(value);
        } else {
            static_assert(dependent_false_v<T>,
                          "mixed-endian native byte order requires an explicit platform policy");
            return value;
        }
    }

    template <unsigned_integer T> [[nodiscard]] constexpr T little_to_native(T value) noexcept {
        return native_to_little(value);
    }

    template <unsigned_integer T> [[nodiscard]] constexpr T native_to_big(T value) noexcept {
        if constexpr (std::endian::native == std::endian::big) {
            return value;
        } else if constexpr (std::endian::native == std::endian::little) {
            return byte_swap(value);
        } else {
            static_assert(dependent_false_v<T>,
                          "mixed-endian native byte order requires an explicit platform policy");
            return value;
        }
    }

    template <unsigned_integer T> [[nodiscard]] constexpr T big_to_native(T value) noexcept {
        return native_to_big(value);
    }

    template <unsigned_integer T, std::size_t Width>
    [[nodiscard]] constexpr T low_bit_mask() noexcept {
        static_assert(Width <= bit_count_v<T>);

        if constexpr (Width == 0U) {
            return T{0};
        } else if constexpr (Width == bit_count_v<T>) {
            return static_cast<T>(~T{0});
        } else {
            return static_cast<T>((T{1} << Width) - T{1});
        }
    }

    template <unsigned_integer T, std::size_t Bit> [[nodiscard]] constexpr T bit_mask() noexcept {
        static_assert(Bit < bit_count_v<T>);
        return static_cast<T>(T{1} << Bit);
    }

    template <unsigned_integer T, std::size_t Bit>
    [[nodiscard]] constexpr bool test_bit(T value) noexcept {
        return (value & bit_mask<T, Bit>()) != T{0};
    }

    template <unsigned_integer T, std::size_t Bit>
    [[nodiscard]] constexpr T set_bit(T value) noexcept {
        return static_cast<T>(value | bit_mask<T, Bit>());
    }

    template <unsigned_integer T, std::size_t Bit>
    [[nodiscard]] constexpr T clear_bit(T value) noexcept {
        return static_cast<T>(value & ~bit_mask<T, Bit>());
    }

    template <unsigned_integer T, std::size_t Bit>
    [[nodiscard]] constexpr T write_bit(T value, bool enabled) noexcept {
        return enabled ? set_bit<T, Bit>(value) : clear_bit<T, Bit>(value);
    }

    template <unsigned_integer T, std::size_t Offset, std::size_t Width>
    [[nodiscard]] constexpr T bit_field_mask() noexcept {
        static_assert(Offset < bit_count_v<T>);
        static_assert(Width <= bit_count_v<T> - Offset);
        return static_cast<T>(low_bit_mask<T, Width>() << Offset);
    }

    template <unsigned_integer T, std::size_t Offset, std::size_t Width>
    [[nodiscard]] constexpr T extract_bits(T value) noexcept {
        static_assert(Offset < bit_count_v<T>);
        static_assert(Width <= bit_count_v<T> - Offset);
        return static_cast<T>((value >> Offset) & low_bit_mask<T, Width>());
    }

    template <unsigned_integer T, std::size_t Offset, std::size_t Width>
    [[nodiscard]] constexpr T replace_bits(T value, T field_value) noexcept {
        const T mask = bit_field_mask<T, Offset, Width>();
        return static_cast<T>((value & static_cast<T>(~mask)) | ((field_value << Offset) & mask));
    }

} // namespace mnemos::foundation
