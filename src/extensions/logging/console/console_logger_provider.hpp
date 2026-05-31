#pragma once

#include "ilogger.hpp"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace mnemos::logging::console {

    // The canonical console line for a record, terminated with '\n':
    // "[level] t=<ns> <category>: <message> k=v ... @file:line". Exposed (and
    // pure) so the format is unit-testable without a stream.
    [[nodiscard]] std::string format_console_line(const log_record_view& record,
                                                  std::string_view category);

    // Writes formatted records to a C stream (stderr by default). Every logger it
    // creates shares the stream and one mutex, so records never interleave across
    // categories. The shared mutex outlives both the provider and its loggers.
    class console_logger_provider final : public ilogger_provider {
      public:
        explicit console_logger_provider(std::FILE* stream = stderr) noexcept;

        [[nodiscard]] std::unique_ptr<ilogger> create_logger(std::string_view category) override;

      private:
        std::FILE* stream_;
        std::shared_ptr<std::mutex> mutex_;
    };

} // namespace mnemos::logging::console
