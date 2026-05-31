#pragma once

#include "ilogger.hpp"

#include <atomic>
#include <memory>
#include <string_view>
#include <vector>

namespace mnemos::logging {

    // Owns a set of providers and a global minimum level; hands out category
    // loggers that fan each record out to every provider. Modeled on .NET's
    // ILoggerFactory. The factory must outlive every logger it creates -- its
    // loggers read the live minimum level by reference.
    class logger_factory final {
      public:
        logger_factory() = default;
        logger_factory(const logger_factory&) = delete;
        logger_factory& operator=(const logger_factory&) = delete;

        void add_provider(std::unique_ptr<ilogger_provider> provider);

        void set_minimum_level(log_level level) noexcept {
            minimum_level_.store(level, std::memory_order_relaxed);
        }
        [[nodiscard]] log_level minimum_level() const noexcept {
            return minimum_level_.load(std::memory_order_relaxed);
        }

        // A logger for `category` whose log()/is_enabled() reflect the factory's
        // live minimum level and the current provider set at creation time.
        [[nodiscard]] std::unique_ptr<ilogger> create_logger(std::string_view category);

      private:
        std::vector<std::unique_ptr<ilogger_provider>> providers_;
        std::atomic<log_level> minimum_level_{log_level::trace};
    };

} // namespace mnemos::logging
