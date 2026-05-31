#include "logger_factory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    using namespace mnemos::logging;

    // A provider whose loggers record every received record into a shared vector,
    // stamping each with the category they were created for.
    struct recording_provider final : ilogger_provider {
        struct entry final {
            std::string category;
            log_level level;
            std::string message;
        };

        explicit recording_provider(std::vector<entry>& sink) noexcept : sink_(sink) {}

        class recording_logger final : public ilogger {
          public:
            recording_logger(std::vector<entry>& sink, std::string category) noexcept
                : sink_(sink), category_(std::move(category)) {}
            [[nodiscard]] bool is_enabled(log_level) const noexcept override { return true; }
            void log(const log_record_view& record) noexcept override {
                sink_.push_back({category_, record.level, std::string(record.message)});
            }

          private:
            std::vector<entry>& sink_;
            std::string category_;
        };

        [[nodiscard]] std::unique_ptr<ilogger> create_logger(std::string_view category) override {
            return std::make_unique<recording_logger>(sink_, std::string(category));
        }

        std::vector<entry>& sink_;
    };

} // namespace

TEST_CASE("logger_factory fans each record out to every provider", "[logging]") {
    std::vector<recording_provider::entry> a;
    std::vector<recording_provider::entry> b;
    logger_factory factory;
    factory.add_provider(std::make_unique<recording_provider>(a));
    factory.add_provider(std::make_unique<recording_provider>(b));

    auto log = factory.create_logger("genesis.vdp");
    log->log(log_level::info, "hello");

    REQUIRE(a.size() == 1U);
    REQUIRE(b.size() == 1U);
    CHECK(a[0].category == "genesis.vdp");
    CHECK(a[0].level == log_level::info);
    CHECK(a[0].message == "hello");
}

TEST_CASE("logger_factory minimum level gates records and reconfigures live", "[logging]") {
    std::vector<recording_provider::entry> a;
    logger_factory factory;
    factory.add_provider(std::make_unique<recording_provider>(a));
    factory.set_minimum_level(log_level::warning);

    auto log = factory.create_logger("x");
    CHECK_FALSE(log->is_enabled(log_level::info));
    CHECK(log->is_enabled(log_level::error));

    log->log(log_level::info, "dropped");
    log->log(log_level::error, "kept");
    REQUIRE(a.size() == 1U);
    CHECK(a[0].message == "kept");

    // Lowering the floor takes effect on the already-created logger.
    factory.set_minimum_level(log_level::trace);
    CHECK(log->is_enabled(log_level::trace));
    log->log(log_level::trace, "now-kept");
    REQUIRE(a.size() == 2U);
    CHECK(a[1].message == "now-kept");
}

TEST_CASE("log_level off is never emitted", "[logging]") {
    std::vector<recording_provider::entry> a;
    logger_factory factory;
    factory.add_provider(std::make_unique<recording_provider>(a));

    // off as a minimum silences every level...
    factory.set_minimum_level(log_level::off);
    auto log = factory.create_logger("x");
    log->log(log_level::fatal, "nope");
    CHECK(a.empty());

    // ...and off is never an emittable level, even under a permissive minimum.
    factory.set_minimum_level(log_level::trace);
    log->log(log_level::off, "still nope");
    CHECK(a.empty());
}

TEST_CASE("a logger with no providers is a harmless sink", "[logging]") {
    logger_factory factory;
    auto log = factory.create_logger("x");
    CHECK(log->is_enabled(log_level::info)); // gated only by the (default) level
    log->log(log_level::info, "into the void");
}
