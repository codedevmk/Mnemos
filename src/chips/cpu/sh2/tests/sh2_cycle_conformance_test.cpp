// Allow std::getenv on MSVC (test-only; reads a corpus-path env var).
#define _CRT_SECURE_NO_WARNINGS

// SH-2 CYCLE conformance harness (ADR-0026). Validates the per-instruction cycle
// count returned by step_instruction() against a MANUAL-derived corpus
// (SH7600 Programming Manual instruction states + load-use rule, SH7604 Hardware
// Manual bus/cache, 32X Hardware Manual access timing). The manuals are the
// authority; Emu/Ymir are L5 advisory cross-checks only and never supply an
// expected value (CONSTITUTION 2.3). See docs/sh2-cycle-ledger.md for the ledger
// and the vector schema, and docs/plans/2026-06-12-sh2-x2-x3-cycle-true.md.
//
// Data-gated: SKIPs (Catch2 exits 4) unless MNEMOS_SH2_CYCLE_TESTS_DIR points at
// a directory of authored .json vectors (never committed). Each case enables the
// timing models it exercises (`model.load_use` / `model.bus_contention`), loads
// an initial register file, places a 5-word fetch frame, runs one
// step_instruction() per `steps[]` entry comparing the returned cycle count, and
// (when `final` is present) compares the register file — so a timing vector also
// guards semantics. The internal-timing corpus runs a uniform bus (no bus-wait
// hook); the X3 region/contention corpus adds a bus map in a later increment.

#include "ibus.hpp"
#include "sh2.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

    using mnemos::chips::cpu::sh2;

    // Big-endian byte bus over a sparse map; an un-preloaded address is an
    // out-of-flow fetch served as the `opcodes[4]` probe word.
    class corpus_bus final : public mnemos::chips::ibus {
      public:
        std::unordered_map<std::uint32_t, std::uint8_t> memory;
        std::uint16_t out_of_flow = 0x322CU; // opcodes[4]

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            const auto it = memory.find(address);
            if (it != memory.end()) {
                return it->second;
            }
            return static_cast<std::uint8_t>((address & 1U) != 0U ? (out_of_flow & 0xFFU)
                                                                  : (out_of_flow >> 8U));
        }
        void write8(std::uint32_t address, std::uint8_t value) override { memory[address] = value; }
    };

    void put_be(corpus_bus& bus, std::uint32_t addr, std::uint32_t value, int size) {
        for (int b = 0; b < size; ++b) {
            bus.memory[addr + static_cast<std::uint32_t>(b)] =
                static_cast<std::uint8_t>(value >> (8 * (size - 1 - b)));
        }
    }

    void load_state(const nlohmann::json& s, sh2& cpu) {
        sh2::registers r;
        const auto& regs = s.at("R");
        for (std::size_t i = 0; i < 16U; ++i) {
            r.r[i] = regs.at(i).get<std::uint32_t>();
        }
        r.pc = s.at("PC").get<std::uint32_t>();
        r.pr = s.at("PR").get<std::uint32_t>();
        r.sr = s.at("SR").get<std::uint32_t>();
        r.gbr = s.at("GBR").get<std::uint32_t>();
        r.vbr = s.at("VBR").get<std::uint32_t>();
        r.mach = s.at("MACH").get<std::uint32_t>();
        r.macl = s.at("MACL").get<std::uint32_t>();
        cpu.set_registers(r);
    }

    [[nodiscard]] bool final_state_matches(const nlohmann::json& fin, const sh2& cpu) {
        const auto r = cpu.cpu_registers();
        constexpr std::uint32_t mask = sh2::sr_mask;
        bool ok = r.pc == fin.at("PC").get<std::uint32_t>() &&
                  r.pr == fin.at("PR").get<std::uint32_t>() &&
                  r.gbr == fin.at("GBR").get<std::uint32_t>() &&
                  r.vbr == fin.at("VBR").get<std::uint32_t>() &&
                  r.mach == fin.at("MACH").get<std::uint32_t>() &&
                  r.macl == fin.at("MACL").get<std::uint32_t>() &&
                  r.sr == (fin.at("SR").get<std::uint32_t>() & mask);
        const auto& freg = fin.at("R");
        for (std::size_t i = 0; i < 16U && ok; ++i) {
            ok = r.r[i] == freg.at(i).get<std::uint32_t>();
        }
        return ok;
    }

    struct file_result final {
        std::size_t passed = 0;
        std::size_t failed = 0;
    };

    file_result run_file(const std::filesystem::path& path, std::vector<std::string>& failures) {
        file_result result;
        std::ifstream stream(path);
        const nlohmann::json doc = nlohmann::json::parse(stream);

        std::size_t index = 0;
        for (const auto& test : doc) {
            const auto name = path.filename().string() + "#" + std::to_string(index++);

            corpus_bus bus;
            sh2 cpu;
            cpu.attach_bus(bus);
            if (test.contains("model")) {
                const auto& m = test.at("model");
                cpu.set_load_use_interlock(m.value("load_use", false));
                cpu.set_shared_contention_metering(m.value("bus_contention", false));
            }
            // Optional region-bus map: install a wait-state callback so X3 vectors
            // can charge per-region waits (mirrors the board's bus_wait hook). X2
            // internal vectors omit "bus" and run on the plain uniform bus. The
            // callback returns wait*words (a 4-byte access = two 16-bit bus cycles).
            if (test.contains("bus")) {
                struct wait_range final {
                    std::uint32_t start;
                    std::uint32_t size;
                    int wait;
                };
                std::vector<wait_range> ranges;
                for (const auto& rg : test.at("bus")) {
                    ranges.push_back({rg.at("start").get<std::uint32_t>(),
                                      rg.at("size").get<std::uint32_t>(),
                                      rg.at("wait").get<int>()});
                }
                cpu.set_bus_wait_callback(
                    [ranges](std::uint32_t addr, std::uint8_t bytes,
                             mnemos::chips::cpu::data_access_kind /*kind*/) -> int {
                        const int words = (bytes >= 4U) ? 2 : 1;
                        for (const auto& rg : ranges) {
                            if (addr >= rg.start && addr < rg.start + rg.size) {
                                return rg.wait * words;
                            }
                        }
                        return 0;
                    });
            }

            const auto& init = test.at("initial");
            const auto pc = init.at("PC").get<std::uint32_t>();
            const auto& opcodes = test.at("opcodes");
            bus.out_of_flow = static_cast<std::uint16_t>(opcodes.at(4).get<std::uint32_t>());
            for (int i = 0; i < 4; ++i) {
                put_be(bus, pc + static_cast<std::uint32_t>(2 * i),
                       opcodes.at(static_cast<std::size_t>(i)).get<std::uint32_t>(), 2);
            }
            load_state(init, cpu);

            bool ok = true;
            std::string detail;
            const auto& steps = test.at("steps");
            int step_index = 0;
            for (const auto& step : steps) {
                const int got = cpu.step_instruction();
                const int expected = step.at("cycles").get<int>();
                if (got != expected) {
                    ok = false;
                    detail = " step " + std::to_string(step_index) + " got " + std::to_string(got) +
                             " expected " + std::to_string(expected);
                    break;
                }
                ++step_index;
            }
            if (ok && test.contains("final") && !final_state_matches(test.at("final"), cpu)) {
                ok = false;
                detail = " final-state mismatch";
            }

            if (ok) {
                ++result.passed;
            } else {
                ++result.failed;
                if (failures.size() < 40U) {
                    failures.push_back(name + detail);
                }
            }
        }
        return result;
    }

} // namespace

TEST_CASE("sh2 cycle conformance against the manual-derived corpus") {
    const char* dir = std::getenv("MNEMOS_SH2_CYCLE_TESTS_DIR");
    if (dir == nullptr || std::string{dir}.empty() || !std::filesystem::is_directory(dir)) {
        SKIP("set MNEMOS_SH2_CYCLE_TESTS_DIR to a directory of authored SH-2 cycle vectors "
             "(see docs/sh2-cycle-ledger.md; ADR-0026)");
    }

    file_result totals;
    std::vector<std::string> failures;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        ++files;
        const file_result fr = run_file(entry.path(), failures);
        totals.passed += fr.passed;
        totals.failed += fr.failed;
    }

    INFO("SH-2 cycle conformance: " << totals.passed << " passed, " << totals.failed
                                    << " failed across " << files << " files");
    for (const auto& f : failures) {
        std::cerr << "[sh2-cycle] FAIL " << f << "\n";
    }
    REQUIRE(files > 0);
    CHECK(totals.failed == 0);
}
