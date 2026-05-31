#pragma once

#include <cstdint>
#include <string_view>

namespace mnemos::logging {

    // Severity, ascending. `off` is not a real severity -- it sits above `fatal`
    // so that setting it as a minimum level silences every record.
    enum class log_level : std::uint8_t {
        trace,
        debug,
        info,
        warning,
        error,
        fatal,
        off,
    };

    [[nodiscard]] constexpr std::string_view log_level_name(log_level level) noexcept {
        switch (level) {
        case log_level::trace:
            return "trace";
        case log_level::debug:
            return "debug";
        case log_level::info:
            return "info";
        case log_level::warning:
            return "warning";
        case log_level::error:
            return "error";
        case log_level::fatal:
            return "fatal";
        case log_level::off:
            return "off";
        }

        return "unknown";
    }

    [[nodiscard]] constexpr bool log_level_enabled(log_level level, log_level minimum) noexcept {
        return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(minimum);
    }

} // namespace mnemos::logging
