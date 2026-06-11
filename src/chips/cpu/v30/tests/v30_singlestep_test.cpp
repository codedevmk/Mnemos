// Functional V30 conformance against a public per-instruction JSON corpus for
// the 8088/V20 CPU family (initial/final register + RAM images per test).
// Data-gated by MNEMOS_V30_TESTS_DIR (see THIRD-PARTY.md). Cycle-count and
// bus-order comparison arrive with the timing increment (plan A3/A4);
// MNEMOS_V30_FLAGS_MASK (hex) optionally masks undefined-flag bits until the
// per-instruction masks land.

#include "v30.hpp"

#include "ibus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::cpu::v30;

    // Flat 1 MiB physical memory with a write log so each test's footprint can
    // be reverted without re-clearing the whole array.
    class corpus_bus final : public mnemos::chips::ibus {
      public:
        std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(0x100000U, 0U);
        std::vector<std::uint32_t> dirty;

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFFU];
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            const std::uint32_t addr = address & 0xFFFFFU;
            memory[addr] = value;
            dirty.push_back(addr);
        }

        void poke(std::uint32_t address, std::uint8_t value) {
            const std::uint32_t addr = address & 0xFFFFFU;
            memory[addr] = value;
            dirty.push_back(addr);
        }

        void revert() {
            for (const std::uint32_t addr : dirty) {
                memory[addr] = 0U;
            }
            dirty.clear();
        }
    };

    // Overlay the registers present in `node` onto `base` (the corpus encodes
    // final state sparsely: only changed registers appear).
    [[nodiscard]] v30::registers read_regs(const nlohmann::json& node, const v30::registers& base) {
        v30::registers r = base;
        const auto get = [&](const char* name, std::uint16_t& slot) {
            if (node.contains(name)) {
                slot = node.at(name).get<std::uint16_t>();
            }
        };
        get("ax", r.ax);
        get("bx", r.bx);
        get("cx", r.cx);
        get("dx", r.dx);
        get("si", r.si);
        get("di", r.di);
        get("bp", r.bp);
        get("sp", r.sp);
        get("ip", r.ip);
        get("cs", r.cs);
        get("ds", r.ds);
        get("es", r.es);
        get("ss", r.ss);
        get("flags", r.flags);
        return r;
    }

    std::pair<std::size_t, std::size_t> run_file(const std::filesystem::path& path,
                                                 std::uint16_t flags_mask,
                                                 std::vector<std::string>& failures) {
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::ifstream stream(path);
        const nlohmann::json doc = nlohmann::json::parse(stream);

        corpus_bus bus;
        v30 cpu;
        cpu.attach_bus(bus);

        for (const auto& test : doc) {
            const auto initial = read_regs(test.at("initial").at("regs"), v30::registers{});
            for (const auto& cell : test.at("initial").at("ram")) {
                bus.poke(cell.at(0).get<std::uint32_t>(), cell.at(1).get<std::uint8_t>());
            }
            cpu.set_registers(initial);

            cpu.step_instruction();

            const auto expected = read_regs(test.at("final").at("regs"), initial);
            const auto got = cpu.cpu_registers();
            bool ok = got.ax == expected.ax && got.bx == expected.bx && got.cx == expected.cx &&
                      got.dx == expected.dx && got.si == expected.si && got.di == expected.di &&
                      got.bp == expected.bp && got.sp == expected.sp && got.ip == expected.ip &&
                      got.cs == expected.cs && got.ds == expected.ds && got.es == expected.es &&
                      got.ss == expected.ss &&
                      (got.flags & flags_mask) == (expected.flags & flags_mask);

            for (const auto& cell : test.at("final").at("ram")) {
                if (bus.memory[cell.at(0).get<std::uint32_t>() & 0xFFFFFU] !=
                    cell.at(1).get<std::uint8_t>()) {
                    ok = false;
                }
            }

            if (ok) {
                ++passed;
            } else {
                ++failed;
                if (failures.size() < 20U) {
                    failures.push_back(path.filename().string() + ": " +
                                       test.at("name").get<std::string>());
                }
            }
            bus.revert();
        }

        return {passed, failed};
    }

} // namespace

TEST_CASE("v30 passes the public per-instruction 8088/V20 conformance corpus",
          "[conformance][v30]") {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: data-gated test knobs
#endif
    const char* dir = std::getenv("MNEMOS_V30_TESTS_DIR");
    if (dir == nullptr || std::string{dir}.empty() || !std::filesystem::is_directory(dir)) {
        SKIP("set MNEMOS_V30_TESTS_DIR to a directory of per-instruction 8088/V20 "
             "test JSON files (see THIRD-PARTY.md)");
    }

    std::uint16_t flags_mask = 0xFFFFU;
    if (const char* mask_env = std::getenv("MNEMOS_V30_FLAGS_MASK");
        mask_env != nullptr && *mask_env != '\0') {
        flags_mask = static_cast<std::uint16_t>(std::strtoul(mask_env, nullptr, 16));
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t files = 0;
    std::vector<std::string> failures;
    std::string tally;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        const auto [file_passed, file_failed] = run_file(entry.path(), flags_mask, failures);
        passed += file_passed;
        failed += file_failed;
        ++files;
        if (file_failed > 0U) {
            tally += " " + entry.path().stem().string() + "=" + std::to_string(file_failed);
        }
    }

    std::string detail;
    for (const std::string& failure : failures) {
        detail += "\n  " + failure;
    }
    INFO("files=" << files << " passed=" << passed << " failed=" << failed
                  << "\nper-file failures:" << tally << "\nexamples:" << detail);
    CHECK(files > 0U);
    CHECK(failed == 0U);
}
