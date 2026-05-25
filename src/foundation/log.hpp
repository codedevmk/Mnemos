#pragma once

#include "time.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <span>
#include <string_view>

namespace mnemos::foundation {

    enum class log_level : std::uint8_t {
        trace,
        debug,
        info,
        warning,
        error,
        fatal,
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
        }

        return "unknown";
    }

    [[nodiscard]] constexpr bool log_level_enabled(log_level level, log_level minimum) noexcept {
        return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(minimum);
    }

    struct log_field final {
        std::string_view name;
        std::string_view value;
    };

    struct log_source final {
        std::string_view file;
        std::uint32_t line{};
        std::string_view function;
    };

    struct log_record_view final {
        steady_time timestamp;
        log_level level{log_level::info};
        std::string_view channel;
        std::string_view message;
        std::span<const log_field> fields{};
        log_source source{};
    };

    class log_sink {
      public:
        log_sink() = default;
        log_sink(const log_sink&) = delete;
        log_sink& operator=(const log_sink&) = delete;
        virtual ~log_sink() = default;

        // The record only borrows caller-owned views and is valid for this call.
        [[nodiscard]] virtual bool write(const log_record_view& record) noexcept = 0;
    };

    class logger final {
      public:
        // The sink pointer storage is borrowed and must outlive the logger.
        explicit logger(std::span<log_sink* const> sinks,
                        log_level minimum_level = log_level::trace) noexcept
            : sinks_(sinks), minimum_level_(minimum_level) {}

        [[nodiscard]] log_level minimum_level() const noexcept {
            return minimum_level_.load(std::memory_order_relaxed);
        }

        void set_minimum_level(log_level minimum_level) noexcept {
            minimum_level_.store(minimum_level, std::memory_order_relaxed);
        }

        [[nodiscard]] bool accepts(log_level level) const noexcept {
            return log_level_enabled(level, minimum_level());
        }

        [[nodiscard]] std::size_t write(const log_record_view& record) const noexcept {
            if (!accepts(record.level)) {
                return 0;
            }

            std::size_t delivered = 0;
            for (log_sink* sink : sinks_) {
                if (sink != nullptr && sink->write(record)) {
                    ++delivered;
                }
            }

            return delivered;
        }

        [[nodiscard]] std::size_t log(steady_time timestamp, log_level level,
                                      std::string_view channel, std::string_view message,
                                      std::span<const log_field> fields = {},
                                      log_source source = {}) const noexcept {
            return write(log_record_view{
                .timestamp = timestamp,
                .level = level,
                .channel = channel,
                .message = message,
                .fields = fields,
                .source = source,
            });
        }

      private:
        std::span<log_sink* const> sinks_;
        std::atomic<log_level> minimum_level_;
    };

    class c_file_log_sink final : public log_sink {
      public:
        explicit c_file_log_sink(std::FILE* stream) noexcept : stream_(stream) {}

        [[nodiscard]] std::FILE* stream() const noexcept { return stream_; }

        [[nodiscard]] bool write(const log_record_view& record) noexcept override {
            if (stream_ == nullptr) {
                return false;
            }

            // Serialize the whole record so concurrent writers cannot interleave fragments.
            const std::lock_guard<std::mutex> guard(mutex_);

            bool ok = write_view("[");
            ok = ok && write_view(log_level_name(record.level));
            ok = ok && write_view("] t=");
            ok = ok && write_timestamp(record.timestamp);

            if (!record.channel.empty()) {
                ok = ok && write_view(" ");
                ok = ok && write_view(record.channel);
            }

            if (!record.message.empty()) {
                ok = ok && write_view(": ");
                ok = ok && write_view(record.message);
            }

            for (const log_field& field : record.fields) {
                ok = ok && write_view(" ");
                ok = ok && write_view(field.name);
                ok = ok && write_view("=");
                ok = ok && write_view(field.value);
            }

            if (!record.source.file.empty()) {
                ok = ok && write_view(" @");
                ok = ok && write_view(record.source.file);
                ok = ok && write_view(":");
                ok = ok && write_u32(record.source.line);
            }

            ok = ok && write_view("\n");
            return ok;
        }

      private:
        [[nodiscard]] bool write_view(std::string_view value) noexcept {
            return value.empty() ||
                   std::fwrite(value.data(), 1U, value.size(), stream_) == value.size();
        }

        [[nodiscard]] bool write_timestamp(steady_time timestamp) noexcept {
            const auto ticks =
                std::chrono::duration_cast<nanoseconds>(timestamp.time_since_epoch()).count();
            return std::fprintf(stream_, "%lld", static_cast<long long>(ticks)) >= 0;
        }

        [[nodiscard]] bool write_u32(std::uint32_t value) noexcept {
            return std::fprintf(stream_, "%u", value) >= 0;
        }

        std::FILE* stream_;
        std::mutex mutex_;
    };

    [[nodiscard]] inline c_file_log_sink& stderr_log_sink() noexcept {
        static c_file_log_sink sink{stderr};
        return sink;
    }

} // namespace mnemos::foundation
