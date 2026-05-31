#pragma once

#include "log_record.hpp"

#include <memory>
#include <span>
#include <string_view>

namespace mnemos::logging {

    // A logger bound to a category. Call sites depend only on this interface;
    // concrete loggers come from a logger_factory. Modeled on .NET's ILogger.
    class ilogger {
      public:
        ilogger() = default;
        ilogger(const ilogger&) = delete;
        ilogger& operator=(const ilogger&) = delete;
        virtual ~ilogger() = default;

        [[nodiscard]] virtual bool is_enabled(log_level level) const noexcept = 0;

        // Core sink: emit a fully-formed record. The record's borrowed views are
        // valid only for this call.
        virtual void log(const log_record_view& record) noexcept = 0;

        // Convenience: drops out early when the level is disabled, otherwise
        // stamps the current time and forwards. The category is filled in by the
        // bound leaf logger, so callers leave it implicit here.
        void log(log_level level, std::string_view message, std::span<const log_field> fields = {},
                 log_source source = {}) noexcept;
    };

    // Creates per-category loggers for one output target (console, file, ...).
    // A logger_factory aggregates providers and fans each record across them.
    // Modeled on .NET's ILoggerProvider.
    class ilogger_provider {
      public:
        ilogger_provider() = default;
        ilogger_provider(const ilogger_provider&) = delete;
        ilogger_provider& operator=(const ilogger_provider&) = delete;
        virtual ~ilogger_provider() = default;

        [[nodiscard]] virtual std::unique_ptr<ilogger> create_logger(std::string_view category) = 0;
    };

} // namespace mnemos::logging
