#pragma once

#include <expected>
#include <memory>
#include <type_traits>
#include <utility>

namespace mnemos::foundation {

    template <typename T, typename E> using expected = std::expected<T, E>;

    template <typename E> using status = std::expected<void, E>;

    template <typename E> [[nodiscard]] constexpr auto unexpected(E&& error) {
        return std::unexpected<std::decay_t<E>>(std::forward<E>(error));
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr bool has_error(const std::expected<T, E>& result) noexcept {
        return !result.has_value();
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr T* value_if(std::expected<T, E>& result) noexcept {
        return result.has_value() ? std::addressof(*result) : nullptr;
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr const T* value_if(const std::expected<T, E>& result) noexcept {
        return result.has_value() ? std::addressof(*result) : nullptr;
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr E* error_if(std::expected<T, E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr const E* error_if(const std::expected<T, E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

    template <typename E>
    [[nodiscard]] constexpr E* error_if(std::expected<void, E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

    template <typename E>
    [[nodiscard]] constexpr const E* error_if(const std::expected<void, E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

} // namespace mnemos::foundation
