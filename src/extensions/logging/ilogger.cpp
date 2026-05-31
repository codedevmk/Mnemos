#include "ilogger.hpp"

namespace mnemos::logging {

    void ilogger::log(log_level level, std::string_view message, std::span<const log_field> fields,
                      log_source source) noexcept {
        if (!is_enabled(level)) {
            return;
        }
        log(log_record_view{
            .timestamp = foundation::steady_clock::now(),
            .level = level,
            .category = {},
            .message = message,
            .fields = fields,
            .source = source,
        });
    }

} // namespace mnemos::logging
