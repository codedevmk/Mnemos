#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnemos::foundation {

    using id_hash = std::uint64_t;

    inline constexpr id_hash fnv1a_64_offset_basis = 0xCBF2'9CE4'8422'2325ULL;
    inline constexpr id_hash fnv1a_64_prime = 0x0000'0100'0000'01B3ULL;

    [[nodiscard]] constexpr id_hash fnv1a_64(std::string_view text) noexcept {
        id_hash hash = fnv1a_64_offset_basis;
        for (const char value : text) {
            hash ^= static_cast<std::uint8_t>(value);
            hash *= fnv1a_64_prime;
        }

        return hash;
    }

    class string_id final {
      public:
        constexpr string_id() noexcept = default;

        constexpr explicit string_id(std::string_view text) noexcept : value_(fnv1a_64(text)) {}

        [[nodiscard]] static constexpr string_id from_hash(id_hash hash) noexcept {
            string_id id;
            id.value_ = hash;
            return id;
        }

        [[nodiscard]] constexpr id_hash value() const noexcept { return value_; }

        [[nodiscard]] constexpr bool is_empty() const noexcept { return value_ == 0U; }

        [[nodiscard]] friend constexpr bool operator==(string_id lhs,
                                                       string_id rhs) noexcept = default;

      private:
        id_hash value_{};
    };

    using chip_id = string_id;

    namespace literals {

        [[nodiscard]] consteval string_id operator""_mnemos_id(const char* text,
                                                               std::size_t size) noexcept {
            return string_id::from_hash(fnv1a_64(std::string_view{text, size}));
        }

        [[nodiscard]] consteval chip_id operator""_chip_id(const char* text,
                                                           std::size_t size) noexcept {
            return chip_id::from_hash(fnv1a_64(std::string_view{text, size}));
        }

    } // namespace literals

} // namespace mnemos::foundation
