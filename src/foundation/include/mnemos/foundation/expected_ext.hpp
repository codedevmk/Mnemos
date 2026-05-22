#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace mnemos::foundation {

    template <typename E> class unexpected_value final {
      public:
        constexpr explicit unexpected_value(E error) : error_(std::move(error)) {}

        [[nodiscard]] constexpr E& error() & noexcept { return error_; }
        [[nodiscard]] constexpr const E& error() const& noexcept { return error_; }
        [[nodiscard]] constexpr E&& error() && noexcept { return std::move(error_); }

      private:
        E error_;
    };

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
    template <typename E> [[nodiscard]] constexpr auto unexpected(E&& error) {
        return std::unexpected<std::decay_t<E>>(std::forward<E>(error));
    }

    template <typename T, typename E> using expected = std::expected<T, E>;

    template <typename E> using status = std::expected<void, E>;
#else
    template <typename E> [[nodiscard]] constexpr auto unexpected(E&& error) {
        return unexpected_value<std::decay_t<E>>(std::forward<E>(error));
    }

    template <typename T, typename E> class expected final {
      public:
        using value_type = T;
        using error_type = E;

        constexpr expected(const T& value) : storage_(value) {}

        constexpr expected(T&& value) : storage_(std::move(value)) {}

        constexpr expected(unexpected_value<E> error) : storage_(std::move(error).error()) {}

        [[nodiscard]] constexpr bool has_value() const noexcept {
            return std::holds_alternative<T>(storage_);
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

        [[nodiscard]] constexpr T& operator*() & noexcept { return std::get<T>(storage_); }

        [[nodiscard]] constexpr const T& operator*() const& noexcept {
            return std::get<T>(storage_);
        }

        [[nodiscard]] constexpr T* operator->() noexcept {
            return std::addressof(std::get<T>(storage_));
        }

        [[nodiscard]] constexpr const T* operator->() const noexcept {
            return std::addressof(std::get<T>(storage_));
        }

        [[nodiscard]] constexpr E& error() & noexcept { return std::get<E>(storage_); }

        [[nodiscard]] constexpr const E& error() const& noexcept { return std::get<E>(storage_); }

      private:
        std::variant<T, E> storage_;
    };

    template <typename E> class expected<void, E> final {
      public:
        using value_type = void;
        using error_type = E;

        constexpr expected() = default;

        constexpr expected(unexpected_value<E> error) : error_(std::move(error).error()) {}

        [[nodiscard]] constexpr bool has_value() const noexcept { return !error_.has_value(); }

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

        [[nodiscard]] constexpr E& error() & noexcept { return *error_; }

        [[nodiscard]] constexpr const E& error() const& noexcept { return *error_; }

      private:
        std::optional<E> error_{};
    };

    template <typename E> using status = expected<void, E>;
#endif

    template <typename T, typename E>
    [[nodiscard]] constexpr bool has_error(const expected<T, E>& result) noexcept {
        return !result.has_value();
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr T* value_if(expected<T, E>& result) noexcept {
        return result.has_value() ? std::addressof(*result) : nullptr;
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr const T* value_if(const expected<T, E>& result) noexcept {
        return result.has_value() ? std::addressof(*result) : nullptr;
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr E* error_if(expected<T, E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

    template <typename T, typename E>
    [[nodiscard]] constexpr const E* error_if(const expected<T, E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

    template <typename E> [[nodiscard]] constexpr E* error_if(status<E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

    template <typename E>
    [[nodiscard]] constexpr const E* error_if(const status<E>& result) noexcept {
        return result.has_value() ? nullptr : std::addressof(result.error());
    }

} // namespace mnemos::foundation
