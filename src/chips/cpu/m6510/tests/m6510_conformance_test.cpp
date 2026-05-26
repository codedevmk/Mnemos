// Allow std::getenv on MSVC (test-only; reads a corpus-path env var).
#define _CRT_SECURE_NO_WARNINGS

// 6502 / m6510 conformance harness.
//
// Validates the m6510 core against a public per-cycle 6502 test corpus (see
// THIRD-PARTY.md): each test fixes an initial CPU + RAM state, runs exactly one
// instruction, and checks the final CPU + RAM state and the exact per-cycle bus
// trace.
//
// The corpus is large and never committed; the test is data-gated and SKIPs when
// MNEMOS_M6510_TESTS_DIR is unset or empty. The $00/$01 I/O port is disabled
// so those addresses behave as the bare-6502 corpus expects. Opcodes Mnemos does
// not implement for v0.1 (JAM/KIL and the unstable illegals) are skipped; see
// src/chips/cpu/m6510/NOTES.md.

#include "m6510.hpp"

#include "ibus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::cpu::m6510;

    class recording_bus final : public mnemos::chips::ibus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};

        struct access final {
            std::uint16_t address;
            std::uint8_t value;
            bool is_write;
        };
        std::vector<access> trace;

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            const auto addr = static_cast<std::uint16_t>(address & 0xFFFFU);
            const std::uint8_t value = memory[addr];
            trace.push_back({addr, value, false});
            return value;
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            const auto addr = static_cast<std::uint16_t>(address & 0xFFFFU);
            memory[addr] = value;
            trace.push_back({addr, value, true});
        }
    };

    // Opcodes intentionally out of scope for v0.1 (JAM/KIL + unstable illegals).
    [[nodiscard]] bool is_unsupported_opcode(unsigned opcode) {
        static const std::set<unsigned> unsupported = {
            0x02U, 0x12U, 0x22U, 0x32U, 0x42U, 0x52U, 0x62U, 0x72U, // JAM/KIL
            0x92U, 0xB2U, 0xD2U, 0xF2U,                             // JAM/KIL
            0x8BU, 0xABU, 0x93U, 0x9FU, 0x9CU, 0x9EU, 0x9BU, 0xBBU, // unstable
        };
        return unsupported.contains(opcode);
    }

    struct cpu_state final {
        std::uint16_t pc;
        std::uint8_t sp;
        std::uint8_t a;
        std::uint8_t x;
        std::uint8_t y;
        std::uint8_t p;
    };

    [[nodiscard]] cpu_state read_state(const nlohmann::json& node) {
        return cpu_state{
            node.at("pc").get<std::uint16_t>(), node.at("s").get<std::uint8_t>(),
            node.at("a").get<std::uint8_t>(),   node.at("x").get<std::uint8_t>(),
            node.at("y").get<std::uint8_t>(),   node.at("p").get<std::uint8_t>(),
        };
    }

    // Run one corpus file; returns {passed, failed} for that file and records up
    // to a few example failure names.
    std::pair<std::size_t, std::size_t> run_file(const std::filesystem::path& path,
                                                 std::vector<std::string>& failures) {
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::ifstream stream(path);
        const nlohmann::json doc = nlohmann::json::parse(stream);

        recording_bus bus;
        m6510 cpu;
        cpu.set_port_enabled(false);
        cpu.attach_bus(bus);

        for (const auto& test : doc) {
            const auto initial = read_state(test.at("initial"));
            for (const auto& cell : test.at("initial").at("ram")) {
                bus.memory[cell.at(0).get<std::uint16_t>()] = cell.at(1).get<std::uint8_t>();
            }

            cpu.set_registers({initial.a, initial.x, initial.y, initial.sp, initial.p, initial.pc});
            bus.trace.clear();

            cpu.tick(1U);
            while (!cpu.at_instruction_boundary()) {
                cpu.tick(1U);
            }

            bool ok = true;
            const auto final_state = read_state(test.at("final"));
            const auto& r = cpu.cpu_registers();
            ok = ok && r.pc == final_state.pc && r.sp == final_state.sp && r.a == final_state.a &&
                 r.x == final_state.x && r.y == final_state.y && r.p == final_state.p;

            for (const auto& cell : test.at("final").at("ram")) {
                if (bus.memory[cell.at(0).get<std::uint16_t>()] != cell.at(1).get<std::uint8_t>()) {
                    ok = false;
                }
            }

            const auto& cycles = test.at("cycles");
            if (cycles.size() != bus.trace.size()) {
                ok = false;
            } else {
                for (std::size_t i = 0; i < bus.trace.size(); ++i) {
                    const bool is_write = cycles[i].at(2).get<std::string>() == "write";
                    if (bus.trace[i].address != cycles[i].at(0).get<std::uint16_t>() ||
                        bus.trace[i].value != cycles[i].at(1).get<std::uint8_t>() ||
                        bus.trace[i].is_write != is_write) {
                        ok = false;
                        break;
                    }
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
        }

        return {passed, failed};
    }

} // namespace

TEST_CASE("m6510 passes the public 6502 conformance corpus", "[conformance]") {
    const char* dir = std::getenv("MNEMOS_M6510_TESTS_DIR");
    if (dir == nullptr || std::string{dir}.empty() || !std::filesystem::is_directory(dir)) {
        SKIP("set MNEMOS_M6510_TESTS_DIR to a directory of per-instruction 6502 test JSON files "
             "(see THIRD-PARTY.md)");
    }

    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t files = 0;
    std::vector<std::string> failures;
    std::string tally;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        const auto opcode = static_cast<unsigned>(std::stoul(stem, nullptr, 16));
        if (is_unsupported_opcode(opcode)) {
            continue;
        }
        const auto [file_passed, file_failed] = run_file(entry.path(), failures);
        passed += file_passed;
        failed += file_failed;
        ++files;
        if (file_failed > 0U) {
            tally += " " + stem + "=" + std::to_string(file_failed);
        }
    }

    std::string detail;
    for (const std::string& failure : failures) {
        detail += "\n  " + failure;
    }
    INFO("files=" << files << " passed=" << passed << " failed=" << failed
                  << "\nper-opcode failures:" << tally << "\nexamples:" << detail);
    CHECK(files > 0U);
    CHECK(failed == 0U);
}
