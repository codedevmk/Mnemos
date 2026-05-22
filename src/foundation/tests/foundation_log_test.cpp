#include <mnemos/foundation/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using namespace std::chrono_literals;

static_assert(std::is_trivially_copyable_v<mnemos::foundation::log_record_view>);

namespace {

    struct captured_field final {
        std::string name;
        std::string value;
    };

    struct captured_record final {
        mnemos::foundation::steady_time timestamp{};
        mnemos::foundation::log_level level{mnemos::foundation::log_level::info};
        std::string channel;
        std::string message;
        std::vector<captured_field> fields;
        std::string source_file;
        std::uint32_t source_line{};
        std::string source_function;
    };

    class capture_sink final : public mnemos::foundation::log_sink {
      public:
        explicit capture_sink(bool result = true) noexcept : result_(result) {}

        [[nodiscard]] bool
        write(const mnemos::foundation::log_record_view& record) noexcept override {
            last.timestamp = record.timestamp;
            last.level = record.level;
            last.channel.assign(record.channel);
            last.message.assign(record.message);
            last.fields.clear();
            last.fields.reserve(record.fields.size());
            for (const mnemos::foundation::log_field& field : record.fields) {
                last.fields.push_back(captured_field{
                    .name = std::string{field.name},
                    .value = std::string{field.value},
                });
            }
            last.source_file.assign(record.source.file);
            last.source_line = record.source.line;
            last.source_function.assign(record.source.function);
            ++count;
            return result_;
        }

        captured_record last{};
        std::size_t count{};

      private:
        bool result_;
    };

} // namespace

TEST_CASE("log levels have stable names and ordering") {
    using mnemos::foundation::log_level;

    CHECK(mnemos::foundation::log_level_name(log_level::trace) == "trace");
    CHECK(mnemos::foundation::log_level_name(log_level::debug) == "debug");
    CHECK(mnemos::foundation::log_level_name(log_level::info) == "info");
    CHECK(mnemos::foundation::log_level_name(log_level::warning) == "warning");
    CHECK(mnemos::foundation::log_level_name(log_level::error) == "error");
    CHECK(mnemos::foundation::log_level_name(log_level::fatal) == "fatal");
    CHECK_FALSE(mnemos::foundation::log_level_enabled(log_level::info, log_level::warning));
    CHECK(mnemos::foundation::log_level_enabled(log_level::error, log_level::warning));
}

TEST_CASE("logger filters records before dispatching to sinks") {
    capture_sink sink;
    mnemos::foundation::log_sink* sinks[]{&sink};
    mnemos::foundation::logger logger{sinks, mnemos::foundation::log_level::warning};
    const auto timestamp = mnemos::foundation::steady_time{10ns};

    CHECK_FALSE(logger.accepts(mnemos::foundation::log_level::info));
    CHECK(logger.log(timestamp, mnemos::foundation::log_level::info, "cpu", "ignored") == 0U);
    CHECK(sink.count == 0U);

    CHECK(logger.accepts(mnemos::foundation::log_level::error));
    CHECK(logger.log(timestamp, mnemos::foundation::log_level::error, "cpu", "irq") == 1U);
    CHECK(sink.count == 1U);
    CHECK(sink.last.timestamp == timestamp);
    CHECK(sink.last.level == mnemos::foundation::log_level::error);
    CHECK(sink.last.channel == "cpu");
    CHECK(sink.last.message == "irq");
}

TEST_CASE("logger preserves structured fields and source metadata") {
    capture_sink sink;
    mnemos::foundation::log_sink* sinks[]{&sink};
    mnemos::foundation::logger logger{sinks};
    const std::array fields{
        mnemos::foundation::log_field{.name = "chip", .value = "mos.6510"},
        mnemos::foundation::log_field{.name = "pc", .value = "c000"},
    };
    const mnemos::foundation::log_source source{
        .file = "src/chips/cpu/m6510/cpu.cpp",
        .line = 42U,
        .function = "tick",
    };

    CHECK(logger.log(mnemos::foundation::steady_time{20ns}, mnemos::foundation::log_level::debug,
                     "trace", "instruction", fields, source) == 1U);

    CHECK(sink.last.fields.size() == fields.size());
    CHECK(sink.last.fields[0].name == "chip");
    CHECK(sink.last.fields[0].value == "mos.6510");
    CHECK(sink.last.fields[1].name == "pc");
    CHECK(sink.last.fields[1].value == "c000");
    CHECK(sink.last.source_file == "src/chips/cpu/m6510/cpu.cpp");
    CHECK(sink.last.source_line == 42U);
    CHECK(sink.last.source_function == "tick");
}

TEST_CASE("logger reports only sinks that accept a record") {
    capture_sink failing_sink{false};
    capture_sink accepting_sink;
    mnemos::foundation::log_sink* sinks[]{&failing_sink, nullptr, &accepting_sink};
    mnemos::foundation::logger logger{sinks};

    const auto delivered = logger.log(mnemos::foundation::steady_time{30ns},
                                      mnemos::foundation::log_level::fatal, "runtime", "halt");

    CHECK(delivered == 1U);
    CHECK(failing_sink.count == 1U);
    CHECK(accepting_sink.count == 1U);
}

namespace {

    [[nodiscard]] std::FILE* open_binary_write(const std::filesystem::path& path) {
#if defined(_MSC_VER)
        std::FILE* stream = nullptr;
        return fopen_s(&stream, path.string().c_str(), "wb") == 0 ? stream : nullptr;
#else
        return std::fopen(path.string().c_str(), "wb");
#endif
    }

    [[nodiscard]] std::string read_file_contents(const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary};
        std::ostringstream out;
        out << stream.rdbuf();
        return out.str();
    }

} // namespace

TEST_CASE("c_file_log_sink writes a complete formatted record") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "mnemos_log_format_test.log";
    std::FILE* stream = open_binary_write(path);
    REQUIRE(stream != nullptr);

    {
        mnemos::foundation::c_file_log_sink sink{stream};
        const std::array fields{
            mnemos::foundation::log_field{.name = "chip", .value = "mos.6510"},
        };
        const mnemos::foundation::log_source source{.file = "cpu.cpp", .line = 42U, .function = ""};
        CHECK(sink.write({.timestamp = mnemos::foundation::steady_time{7ns},
                          .level = mnemos::foundation::log_level::warning,
                          .channel = "cpu",
                          .message = "irq",
                          .fields = fields,
                          .source = source}));
        // Minimal record exercises the empty channel/message/source branches.
        CHECK(sink.write({.timestamp = mnemos::foundation::steady_time{0ns},
                          .level = mnemos::foundation::log_level::trace,
                          .channel = "",
                          .message = "",
                          .fields = {},
                          .source = {}}));
    }
    std::fclose(stream);

    const std::string contents = read_file_contents(path);
    std::filesystem::remove(path);

    CHECK(contents.find("[warning] t=7 cpu: irq chip=mos.6510 @cpu.cpp:42\n") != std::string::npos);
    CHECK(contents.find("[trace] t=0\n") != std::string::npos);
}

TEST_CASE("c_file_log_sink serializes concurrent records into whole lines") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "mnemos_log_concurrency_test.log";
    std::FILE* stream = open_binary_write(path);
    REQUIRE(stream != nullptr);

    constexpr std::size_t thread_count = 8U;
    constexpr std::size_t per_thread = 128U;

    // Catch2 assertion macros are not thread-safe, so workers record success in an atomic and the
    // result is checked on the main thread after join.
    std::atomic<std::size_t> written{0U};
    {
        mnemos::foundation::c_file_log_sink sink{stream};
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (std::size_t t = 0U; t < thread_count; ++t) {
            threads.emplace_back([&sink, &written] {
                for (std::size_t i = 0U; i < per_thread; ++i) {
                    if (sink.write({.timestamp = mnemos::foundation::steady_time{1ns},
                                    .level = mnemos::foundation::log_level::info,
                                    .channel = "cpu",
                                    .message = "concurrent",
                                    .fields = {},
                                    .source = {}})) {
                        written.fetch_add(1U, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }
    std::fclose(stream);

    CHECK(written.load() == thread_count * per_thread);

    const std::string contents = read_file_contents(path);
    std::filesystem::remove(path);

    // With per-record locking every line is a complete record beginning with '['.
    bool all_lines_well_formed = true;
    std::size_t newlines = 0U;
    std::size_t line_start = 0U;
    for (std::size_t i = 0U; i < contents.size(); ++i) {
        if (contents[i] == '\n') {
            if (line_start >= contents.size() || contents[line_start] != '[') {
                all_lines_well_formed = false;
            }
            ++newlines;
            line_start = i + 1U;
        }
    }

    CHECK(all_lines_well_formed);
    CHECK(newlines == thread_count * per_thread);
}
