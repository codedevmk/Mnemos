#pragma once

#include <mnemos/foundation/expected_ext.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::foundation {

    enum class span_error : std::uint8_t {
        offset_out_of_range,
        count_out_of_range,
    };

    template <typename T> using span_result = expected<std::span<T>, span_error>;

    [[nodiscard]] constexpr bool span_contains_range(std::size_t size, std::size_t offset,
                                                     std::size_t count) noexcept {
        return offset <= size && count <= size - offset;
    }

    template <typename T, std::size_t Extent>
    [[nodiscard]] constexpr span_result<T>
    try_subspan(std::span<T, Extent> values, std::size_t offset, std::size_t count) noexcept {
        if (offset > values.size()) {
            return unexpected(span_error::offset_out_of_range);
        }

        if (count > values.size() - offset) {
            return unexpected(span_error::count_out_of_range);
        }

        return values.subspan(offset, count);
    }

    template <typename T, std::size_t Extent>
    [[nodiscard]] constexpr span_result<T> try_first(std::span<T, Extent> values,
                                                     std::size_t count) noexcept {
        return try_subspan(values, 0U, count);
    }

    template <typename T, std::size_t Extent>
    [[nodiscard]] constexpr span_result<T> try_last(std::span<T, Extent> values,
                                                    std::size_t count) noexcept {
        if (count > values.size()) {
            return unexpected(span_error::count_out_of_range);
        }

        return values.subspan(values.size() - count, count);
    }

} // namespace mnemos::foundation
