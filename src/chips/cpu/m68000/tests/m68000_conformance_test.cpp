// Allow std::getenv on MSVC (test-only; reads a corpus-path env var).
#define _CRT_SECURE_NO_WARNINGS

// 680x0 conformance harness for the m68000.
//
// Validates the core against a public per-instruction 68000 test corpus (see
// THIRD-PARTY.md): each test
// fixes an initial CPU + RAM state, runs exactly one instruction, and checks the
// final register file and memory. The corpus is large and never committed; the
// test is data-gated and SKIPs unless MNEMOS_M68000_TESTS_DIR points at the
// decompressed .json files (one per mnemonic, e.g. ADD.w.json).
//
// Mnemos's m68000 is instruction-stepped, not prefetch/cycle-exact, so this
// harness deliberately does NOT compare the final PC or the prefetch queue (the
// corpus encodes the two-word prefetch pipeline) nor the per-cycle bus trace --
// it checks D0-D7, A0-A7/USP/SSP, SR, and the affected memory. That catches the
// arithmetic/flag/addressing/memory bugs that matter for real software while the
// cycle-exact prefetch pipeline remains future work. Cases that rely on the
// 68000's address/bus-error (group-0) traps are expected to diverge here, since
// the functional core completes unaligned accesses instead of trapping.

#include "ibus.hpp"
#include "m68000.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

    using mnemos::chips::cpu::m68000;

    // Sparse 24-bit memory: the corpus touches only a handful of addresses per test.
    class sparse_bus final : public mnemos::chips::ibus {
      public:
        std::unordered_map<std::uint32_t, std::uint8_t> memory;

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            const auto it = memory.find(address & 0xFFFFFFU);
            return it == memory.end() ? std::uint8_t{0} : it->second;
        }
        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address & 0xFFFFFFU] = value;
        }
    };

    void load_registers(const nlohmann::json& node, m68000& cpu) {
        m68000::registers r;
        r.d[0] = node.at("d0").get<std::uint32_t>();
        r.d[1] = node.at("d1").get<std::uint32_t>();
        r.d[2] = node.at("d2").get<std::uint32_t>();
        r.d[3] = node.at("d3").get<std::uint32_t>();
        r.d[4] = node.at("d4").get<std::uint32_t>();
        r.d[5] = node.at("d5").get<std::uint32_t>();
        r.d[6] = node.at("d6").get<std::uint32_t>();
        r.d[7] = node.at("d7").get<std::uint32_t>();
        r.a[0] = node.at("a0").get<std::uint32_t>();
        r.a[1] = node.at("a1").get<std::uint32_t>();
        r.a[2] = node.at("a2").get<std::uint32_t>();
        r.a[3] = node.at("a3").get<std::uint32_t>();
        r.a[4] = node.at("a4").get<std::uint32_t>();
        r.a[5] = node.at("a5").get<std::uint32_t>();
        r.a[6] = node.at("a6").get<std::uint32_t>();
        r.usp = node.at("usp").get<std::uint32_t>();
        r.ssp = node.at("ssp").get<std::uint32_t>();
        r.sr = node.at("sr").get<std::uint16_t>();
        const bool supervisor = (r.sr & m68000::sr_s) != 0U;
        r.a[7] = supervisor ? r.ssp : r.usp; // a7 is the active stack pointer
        // The opcode sits at pc: prefetch[0] is the word at pc, prefetch[1] the word
        // at pc+2; any further extension words come from the ram array (pc+4 onward).
        r.pc = node.at("pc").get<std::uint32_t>();
        cpu.set_registers(r);
    }

    // Known 680x0 corpus anomalies: a tiny set of tests whose expected
    // final state contradicts the 68000 spec (e.g. ASL.b on a Dn that "changes" the
    // upper 24 bits, which a byte shift on a register cannot do). Skipped explicitly so
    // they cannot mask a real future regression.
    [[nodiscard]] bool is_known_corpus_anomaly(const std::string& name) {
        static const std::set<std::string> anomalies = {
            "e502 [ASL.b Q, D2] 1583", // ASL.b #2,D2 expected final mutates the high 24 bits
            "e502 [ASL.b Q, D2] 1761",
        };
        return anomalies.contains(name);
    }

    struct file_result final {
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::size_t skipped = 0; // group-0 (address/bus error) cases, not yet modelled
    };

    file_result run_file(const std::filesystem::path& path, std::size_t max_tests,
                         std::vector<std::string>& failures) {
        file_result result;
        std::ifstream stream(path);
        const nlohmann::json doc = nlohmann::json::parse(stream);

        sparse_bus bus;
        m68000 cpu;
        cpu.attach_bus(bus);

        const auto longword_at = [&bus](std::uint32_t addr) {
            std::uint32_t v = 0;
            for (std::uint32_t i = 0; i < 4U; ++i) {
                const auto it = bus.memory.find((addr + i) & 0xFFFFFFU);
                v = (v << 8U) | (it == bus.memory.end() ? 0U : it->second);
            }
            return v;
        };

        std::size_t count = 0;
        for (const auto& test : doc) {
            if (max_tests != 0U && count >= max_tests) {
                break;
            }
            ++count;

            if (is_known_corpus_anomaly(test.at("name").get<std::string>())) {
                ++result.skipped;
                continue;
            }

            const auto& init = test.at("initial");
            bus.memory.clear();
            // Opcode word at pc, the next word at pc+2 (the prefetch queue); further
            // extension words come from the ram array below.
            const auto pc = init.at("pc").get<std::uint32_t>();
            const auto prefetch = init.at("prefetch");
            const auto w0 = prefetch.at(0).get<std::uint16_t>();
            const auto w1 = prefetch.at(1).get<std::uint16_t>();
            bus.memory[pc & 0xFFFFFFU] = static_cast<std::uint8_t>(w0 >> 8U);
            bus.memory[(pc + 1U) & 0xFFFFFFU] = static_cast<std::uint8_t>(w0);
            bus.memory[(pc + 2U) & 0xFFFFFFU] = static_cast<std::uint8_t>(w1 >> 8U);
            bus.memory[(pc + 3U) & 0xFFFFFFU] = static_cast<std::uint8_t>(w1);
            for (const auto& cell : init.at("ram")) {
                bus.memory[cell.at(0).get<std::uint32_t>() & 0xFFFFFFU] =
                    cell.at(1).get<std::uint8_t>();
            }

            // Group-0 (address/bus error) handler addresses from the vector table,
            // for filtering: the functional core does not raise these traps.
            const std::uint32_t bus_error_handler = longword_at(0x08U);
            const std::uint32_t addr_error_handler = longword_at(0x0CU);

            load_registers(init, cpu);
            (void)cpu.step_instruction();

            const auto& fin = test.at("final");
            // Skip cases the corpus resolves via an address/bus-error trap (vector to
            // the group-0 handler) -- not modelled by the instruction-stepped core.
            const auto final_pc = fin.at("pc").get<std::uint32_t>();
            if ((addr_error_handler != 0U && final_pc == addr_error_handler) ||
                (bus_error_handler != 0U && final_pc == bus_error_handler)) {
                ++result.skipped;
                continue;
            }
            const auto r = cpu.cpu_registers();
            const bool fsuper = (fin.at("sr").get<std::uint16_t>() & m68000::sr_s) != 0U;
            const std::uint32_t actual_usp = fsuper ? r.usp : r.a[7];
            const std::uint32_t actual_ssp = fsuper ? r.a[7] : r.ssp;

            bool ok = r.sr == fin.at("sr").get<std::uint16_t>() &&
                      actual_usp == fin.at("usp").get<std::uint32_t>() &&
                      actual_ssp == fin.at("ssp").get<std::uint32_t>();
            for (int i = 0; i < 8 && ok; ++i) {
                ok = r.d[static_cast<std::size_t>(i)] ==
                     fin.at("d" + std::to_string(i)).get<std::uint32_t>();
            }
            for (int i = 0; i < 7 && ok; ++i) {
                ok = r.a[static_cast<std::size_t>(i)] ==
                     fin.at("a" + std::to_string(i)).get<std::uint32_t>();
            }
            for (const auto& cell : fin.at("ram")) {
                if (!ok) {
                    break;
                }
                const auto addr = cell.at(0).get<std::uint32_t>() & 0xFFFFFFU;
                const auto it = bus.memory.find(addr);
                const std::uint8_t got = it == bus.memory.end() ? std::uint8_t{0} : it->second;
                ok = got == cell.at(1).get<std::uint8_t>();
            }

            if (ok) {
                ++result.passed;
            } else {
                ++result.failed;
                static bool dumped = false;
                if (!dumped && std::getenv("MNEMOS_M68000_DUMP") != nullptr) {
                    dumped = true;
                    std::cerr << "\n=== FIRST FAILURE: " << path.filename().string() << " "
                              << test.at("name").get<std::string>() << " ===\n";
                    std::cerr << std::hex;
                    std::cerr << "sr   got=" << r.sr << " exp=" << fin.at("sr").get<std::uint16_t>()
                              << "\n";
                    std::cerr << "usp  got=" << actual_usp
                              << " exp=" << fin.at("usp").get<std::uint32_t>() << "\n";
                    std::cerr << "ssp  got=" << actual_ssp
                              << " exp=" << fin.at("ssp").get<std::uint32_t>() << "\n";
                    for (int i = 0; i < 8; ++i) {
                        std::cerr << "d" << i << "   got=" << r.d[static_cast<std::size_t>(i)]
                                  << " exp=" << fin.at("d" + std::to_string(i)).get<std::uint32_t>()
                                  << "\n";
                    }
                    for (int i = 0; i < 7; ++i) {
                        std::cerr << "a" << i << "   got=" << r.a[static_cast<std::size_t>(i)]
                                  << " exp=" << fin.at("a" + std::to_string(i)).get<std::uint32_t>()
                                  << "\n";
                    }
                    for (const auto& cell : fin.at("ram")) {
                        const auto addr = cell.at(0).get<std::uint32_t>() & 0xFFFFFFU;
                        const auto fit = bus.memory.find(addr);
                        const std::uint8_t got =
                            fit == bus.memory.end() ? std::uint8_t{0} : fit->second;
                        std::cerr << "ram[" << addr << "] got=" << static_cast<unsigned>(got)
                                  << " exp=" << cell.at(1).get<unsigned>() << "\n";
                    }
                    std::cerr << std::dec;
                }
                if (failures.size() < 25U) {
                    failures.push_back(path.filename().string() + ": " +
                                       test.at("name").get<std::string>());
                }
            }
        }
        return result;
    }

} // namespace

TEST_CASE("m68000 passes the public 680x0 conformance corpus", "[conformance]") {
    const char* dir = std::getenv("MNEMOS_M68000_TESTS_DIR");
    if (dir == nullptr || std::string{dir}.empty() || !std::filesystem::is_directory(dir)) {
        SKIP("set MNEMOS_M68000_TESTS_DIR to a directory of per-instruction 68000 test JSON files "
             "(see THIRD-PARTY.md)");
    }

    std::size_t max_tests = 0; // 0 = all
    if (const char* cap = std::getenv("MNEMOS_M68000_MAX_TESTS"); cap != nullptr) {
        max_tests = static_cast<std::size_t>(std::strtoull(cap, nullptr, 10));
    }
    // Optional: restrict to one mnemonic (substring) for focused debugging.
    const char* only = std::getenv("MNEMOS_M68000_ONLY");

    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    std::size_t files = 0;
    std::vector<std::string> failures;
    std::string tally;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        if (only != nullptr && entry.path().filename().string().find(only) == std::string::npos) {
            continue;
        }
        const auto result = run_file(entry.path(), max_tests, failures);
        passed += result.passed;
        failed += result.failed;
        skipped += result.skipped;
        ++files;
        if (result.failed > 0U) {
            tally += " " + entry.path().stem().string() + "=" + std::to_string(result.failed);
        }
    }

    std::string detail;
    for (const std::string& failure : failures) {
        detail += "\n  " + failure;
    }
    INFO("files=" << files << " passed=" << passed << " failed=" << failed << " skipped(group0)="
                  << skipped << "\nper-file failures:" << tally << "\nexamples:" << detail);
    CHECK(files > 0U);
    CHECK(failed == 0U);
}
