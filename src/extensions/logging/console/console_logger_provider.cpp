#include "console_logger_provider.hpp"

#include "log_level.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace mnemos::logging::console {

    std::string format_console_line(const log_record_view& record, std::string_view category) {
        std::string out;
        out += '[';
        out += log_level_name(record.level);
        out += "] t=";
        const auto ticks =
            std::chrono::duration_cast<foundation::nanoseconds>(record.timestamp.time_since_epoch())
                .count();
        out += std::to_string(static_cast<long long>(ticks));
        if (!category.empty()) {
            out += ' ';
            out += category;
        }
        if (!record.message.empty()) {
            out += ": ";
            out += record.message;
        }
        for (const log_field& field : record.fields) {
            out += ' ';
            out += field.name;
            out += '=';
            out += field.value;
        }
        if (!record.source.file.empty()) {
            out += " @";
            out += record.source.file;
            out += ':';
            out += std::to_string(record.source.line);
        }
        out += '\n';
        return out;
    }

    namespace {

        // One category's view onto the shared stream.
        class console_logger final : public ilogger {
          public:
            console_logger(std::FILE* stream, std::shared_ptr<std::mutex> mutex,
                           std::string category) noexcept
                : stream_(stream), mutex_(std::move(mutex)), category_(std::move(category)) {}

            [[nodiscard]] bool is_enabled(log_level level) const noexcept override {
                return stream_ != nullptr && level != log_level::off;
            }

            void log(const log_record_view& record) noexcept override {
                if (!is_enabled(record.level)) {
                    return;
                }
                const std::string_view category =
                    record.category.empty() ? std::string_view(category_) : record.category;
                const std::string line = format_console_line(record, category);

                // One write under the shared lock so categories never interleave.
                const std::lock_guard<std::mutex> guard(*mutex_);
                std::fwrite(line.data(), 1U, line.size(), stream_);
            }

          private:
            std::FILE* stream_;
            std::shared_ptr<std::mutex> mutex_;
            std::string category_;
        };

    } // namespace

    console_logger_provider::console_logger_provider(std::FILE* stream) noexcept
        : stream_(stream), mutex_(std::make_shared<std::mutex>()) {}

    std::unique_ptr<ilogger> console_logger_provider::create_logger(std::string_view category) {
        return std::make_unique<console_logger>(stream_, mutex_, std::string(category));
    }

} // namespace mnemos::logging::console
