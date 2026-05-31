#pragma once

#include "log_level.hpp"
#include "time.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace mnemos::logging {

    // A structured key/value attached to a record. Both views are borrowed and
    // must outlive the log() call.
    struct log_field final {
        std::string_view name;
        std::string_view value;
    };

    // Where a record was emitted (optional).
    struct log_source final {
        std::string_view file;
        std::uint32_t line{};
        std::string_view function;
    };

    // One log record. Every view is caller-owned and valid only for the duration
    // of the log() call -- loggers that buffer must copy.
    struct log_record_view final {
        foundation::steady_time timestamp;
        log_level level{log_level::info};
        std::string_view category;
        std::string_view message;
        std::span<const log_field> fields{};
        log_source source{};
    };

} // namespace mnemos::logging
