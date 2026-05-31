#include "logger_factory.hpp"

#include <utility>

namespace mnemos::logging {

    namespace {

        // Fans a record across one leaf logger per provider, gated by the
        // factory's live minimum level.
        class composite_logger final : public ilogger {
          public:
            composite_logger(std::vector<std::unique_ptr<ilogger>> children,
                             const std::atomic<log_level>& minimum) noexcept
                : children_(std::move(children)), minimum_(minimum) {}

            [[nodiscard]] bool is_enabled(log_level level) const noexcept override {
                return log_level_enabled(level, minimum_.load(std::memory_order_relaxed));
            }

            void log(const log_record_view& record) noexcept override {
                if (!is_enabled(record.level)) {
                    return;
                }
                for (const auto& child : children_) {
                    if (child) {
                        child->log(record);
                    }
                }
            }

          private:
            std::vector<std::unique_ptr<ilogger>> children_;
            const std::atomic<log_level>& minimum_;
        };

    } // namespace

    void logger_factory::add_provider(std::unique_ptr<ilogger_provider> provider) {
        if (provider) {
            providers_.push_back(std::move(provider));
        }
    }

    std::unique_ptr<ilogger> logger_factory::create_logger(std::string_view category) {
        std::vector<std::unique_ptr<ilogger>> children;
        children.reserve(providers_.size());
        for (const auto& provider : providers_) {
            if (provider) {
                children.push_back(provider->create_logger(category));
            }
        }
        return std::make_unique<composite_logger>(std::move(children), minimum_level_);
    }

} // namespace mnemos::logging
