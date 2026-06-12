// Allow std::getenv on MSVC (test-only; reads a corpus-path env var).
#define _CRT_SECURE_NO_WARNINGS

// SH-2 functional conformance harness against the public SH4 single-step corpus
// (see THIRD-PARTY-REFERENCES.md). The corpus is Reicast-GENERATED (a software
// SH4 interpreter, not hardware), so this validates that our SH-2 matches that
// reference on the SHARED SH-2/SH-4 integer instructions -- it is a functional
// (semantics) cross-check, NOT a cycle-timing reference, and the SH4 superscalar
// cycle data is ignored. MAC.L/MAC.W are absent from the corpus by design (the
// format records one data access per instruction).
//
// Corpus shape: each test fixes an initial SH-4 register file + memory, runs a
// FOUR-instruction frame, and gives the final state:
//   opcodes[0] = NOP, opcodes[1] = the instruction under test, opcodes[2] =
//   ADD R1,R1 (a delay-slot / fall-through probe), opcodes[3] = NOP, and
//   opcodes[4] = ADD R2,R2 served for any out-of-flow fetch (branch detection).
// The frame is four instruction FETCHES; a delayed branch consumes its
// delay-slot fetch inside one step_instruction, so a taken delayed branch needs
// three step_instruction calls to reach four fetches, not four.
//
// Data-gated: SKIPs unless MNEMOS_SH2_TESTS_DIR points at the transcoded .json
// files (run the corpus's transcode_json.py first; the .json.bin form is not
// read here). We compare the SH-2 register file (R0-R15, PC, PR, GBR, VBR,
// MACH, MACL) and SR masked to the SH-2-defined bits; SH-4-only state (FP banks,
// SSR/SPC/SGR/DBR, FPSCR) is ignored. FP encodings (group 0xF) are skipped; any
// other SH-4-only opcode our core rejects surfaces as a failure to be triaged
// into is_known_divergence().

#include "ibus.hpp"
#include "sh2.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

    using mnemos::chips::cpu::sh2;

    // Big-endian byte bus over a sparse map. Any address the test did not
    // pre-load is an out-of-flow instruction fetch, served as the opcodes[4]
    // probe word (ADD R2,R2), so a stray fetch shows up as a register diff.
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

    // Store the low `size` bytes of `value` big-endian at `addr`.
    void put_be(corpus_bus& bus, std::uint32_t addr, std::uint32_t value, int size) {
        for (int b = 0; b < size; ++b) {
            bus.memory[addr + static_cast<std::uint32_t>(b)] =
                static_cast<std::uint8_t>(value >> (8 * (size - 1 - b)));
        }
    }

    // Byte width of an SH-2 data move/RMW opcode, or 0 if it touches no operand
    // memory (so no read/write value needs placing). MAC is absent from the corpus.
    [[nodiscard]] int data_size(std::uint16_t op) {
        const unsigned hi = (op >> 12U) & 0xFU;
        const unsigned lo = op & 0xFU;
        const unsigned mid = (op >> 8U) & 0xFU;
        switch (hi) {
        case 0x0: // MOV.x Rm,@(R0,Rn) (4/5/6) and @(R0,Rm),Rn (C/D/E)
            if (lo == 0x4U || lo == 0xCU) {
                return 1;
            }
            if (lo == 0x5U || lo == 0xDU) {
                return 2;
            }
            if (lo == 0x6U || lo == 0xEU) {
                return 4;
            }
            return 0;
        case 0x1: // MOV.L Rm,@(disp,Rn)
            return 4;
        case 0x2: // MOV.x Rm,@Rn (0/1/2) and @-Rn (4/5/6); TAS.B is 0x4nnn
            if (lo == 0x0U || lo == 0x4U) {
                return 1;
            }
            if (lo == 0x1U || lo == 0x5U) {
                return 2;
            }
            if (lo == 0x2U || lo == 0x6U) {
                return 4;
            }
            return 0;
        case 0x4: // STS.L/LDS.L/STC.L/LDC.L @-Rn/@Rn+ are longword; TAS.B @Rn is byte
            if ((op & 0xFFU) == 0x1BU) {
                return 1; // TAS.B
            }
            if (lo == 0x2U || lo == 0x3U || lo == 0x6U || lo == 0x7U) {
                return 4; // the @-Rn (2/3) and @Rn+ (6/7) system-register forms
            }
            return 0;
        case 0x5: // MOV.L @(disp,Rm),Rn
            return 4;
        case 0x6: // MOV.x @Rm,Rn (0/1/2) and @Rm+,Rn (4/5/6)
            if (lo == 0x0U || lo == 0x4U) {
                return 1;
            }
            if (lo == 0x1U || lo == 0x5U) {
                return 2;
            }
            if (lo == 0x2U || lo == 0x6U) {
                return 4;
            }
            return 0;
        case 0x8: // MOV.B/W R0,@(disp,Rn) (0/1) and @(disp,Rm),R0 (4/5)
            if (mid == 0x0U || mid == 0x4U) {
                return 1;
            }
            if (mid == 0x1U || mid == 0x5U) {
                return 2;
            }
            return 0;
        case 0x9: // MOV.W @(disp,PC),Rn
            return 2;
        case 0xC: // MOV.x R0,@(disp,GBR) (0/1/2), @(disp,GBR),R0 (4/5/6),
                  // and the .B RMW logicals @(R0,GBR) (C/D/E/F)
            if (mid == 0x0U || mid == 0x4U || mid == 0xCU || mid == 0xDU || mid == 0xEU ||
                mid == 0xFU) {
                return 1;
            }
            if (mid == 0x1U || mid == 0x5U) {
                return 2;
            }
            if (mid == 0x2U || mid == 0x6U) {
                return 4;
            }
            return 0;
        case 0xD: // MOV.L @(disp,PC),Rn
            return 4;
        default:
            return 0;
        }
    }

    // True when a `size`-byte data access at `addr` raises an address error on our
    // SH-2 (mirrors require_*_data_access). The corpus's SH4 source (Reicast) does
    // NOT model these faults, so it would complete the access where our core
    // vectors away -- a legitimate divergence we exclude rather than count, the
    // same posture the m68000 harness takes for group-0 traps.
    [[nodiscard]] bool would_address_error(std::uint32_t addr, int size) {
        const bool cache_ctrl = addr >= 0x40000000U && addr < 0x80000000U;
        const bool high_onchip = addr >= 0xFFFFFF00U;
        const bool low_onchip = addr >= 0xFFFFFE00U && addr < 0xFFFFFF00U;
        switch (size) {
        case 1:
            return high_onchip || cache_ctrl;
        case 2:
            return (addr & 1U) != 0U || cache_ctrl;
        case 4:
            return (addr & 3U) != 0U || low_onchip;
        default:
            return false;
        }
    }

    // SH-4-only encodings our SH-2 does not implement: all of group 0xF (the FPU)
    // plus the SH-4 cache/control ops sharing group-0/group-4 slots. Skipping the
    // FPU is the bulk; others surface as failures and get triaged below.
    [[nodiscard]] bool sh4_only(std::uint16_t op) {
        if ((op >> 12U) == 0xFU) {
            return true; // FPU
        }
        // SH-4 cache hints / control sharing the 0x0nF3-family and 0x00x8 slots.
        const unsigned suffix = op & 0xFFU;
        switch (suffix) {
        case 0x83U: // PREF @Rn
        case 0x93U: // OCBI @Rn
        case 0xA3U: // OCBP @Rn
        case 0xB3U: // OCBWB @Rn
        case 0xC3U: // MOVCA.L R0,@Rn
            return true;
        default:
            break;
        }
        switch (op) {
        case 0x0038U: // LDTLB
        case 0x0048U: // RTE quirk slots / SH-4 control (CLRS)
        case 0x0058U: // SETS
        case 0x001BU: // SLEEP behaves; keep
            return op == 0x0038U || op == 0x0048U || op == 0x0058U;
        default:
            return false;
        }
    }

    [[nodiscard]] bool is_unconditional_delayed_branch(std::uint16_t op) {
        if ((op >> 12U) == 0xAU || (op >> 12U) == 0xBU) {
            return true; // BRA / BSR
        }
        const unsigned suffix = op & 0xFFU;
        if ((op >> 12U) == 0x0U && (suffix == 0x23U || suffix == 0x03U)) {
            return true; // BRAF / BSRF
        }
        if ((op >> 12U) == 0x4U && (suffix == 0x2BU || suffix == 0x0BU)) {
            return true; // JMP / JSR
        }
        return op == 0x000BU || op == 0x002BU; // RTS / RTE
    }

    // Number of step_instruction() calls to run a four-fetch frame: a taken
    // delayed branch folds its delay-slot fetch into one step.
    [[nodiscard]] int step_count(std::uint16_t op, std::uint32_t initial_sr) {
        if (is_unconditional_delayed_branch(op)) {
            return 3;
        }
        const bool t = (initial_sr & sh2::sr_t) != 0U;
        if ((op & 0xFF00U) == 0x8D00U) { // BT/S -- delayed when T
            return t ? 3 : 4;
        }
        if ((op & 0xFF00U) == 0x8F00U) { // BF/S -- delayed when not T
            return t ? 4 : 3;
        }
        return 4;
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

    // Known SH-2 vs SH-4 (Reicast) divergences: encodings whose shared mnemonic
    // legitimately differs between the cores (or known corpus quirks), excluded so
    // they cannot mask a real regression. Seeded empty; populated by triaging the
    // failures the first full corpus run reports.
    [[nodiscard]] bool is_known_divergence(std::uint16_t /*op*/, const std::string& /*name*/) {
        return false;
    }

    struct file_result final {
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::size_t skipped = 0;
    };

    file_result run_file(const std::filesystem::path& path, std::size_t max_tests,
                         std::vector<std::string>& failures) {
        file_result result;
        std::ifstream stream(path);
        const nlohmann::json doc = nlohmann::json::parse(stream);

        std::size_t count = 0;
        for (const auto& test : doc) {
            if (max_tests != 0U && count >= max_tests) {
                break;
            }
            ++count;

            const auto& opcodes = test.at("opcodes");
            const auto op = static_cast<std::uint16_t>(opcodes.at(1).get<std::uint32_t>());
            const auto name = path.filename().string() + "#" + std::to_string(count - 1U);
            if (sh4_only(op) || is_known_divergence(op, name)) {
                ++result.skipped;
                continue;
            }

            corpus_bus bus;
            sh2 cpu;
            cpu.attach_bus(bus);

            const auto& init = test.at("initial");
            const auto pc = init.at("PC").get<std::uint32_t>();
            bus.out_of_flow = static_cast<std::uint16_t>(opcodes.at(4).get<std::uint32_t>());
            for (int i = 0; i < 4; ++i) {
                put_be(bus, pc + static_cast<std::uint32_t>(2 * i),
                       opcodes.at(static_cast<std::size_t>(i)).get<std::uint32_t>(), 2);
            }
            const int size = data_size(op);
            const int access_size = size != 0 ? size : 4;

            // Skip cases whose data operand address would raise an address error on
            // our SH-2 but not on the corpus's SH4 source -- our core vectors away
            // where Reicast completes the access, so the final state legitimately
            // differs. The corpus seeds random Rm/Rn, so misaligned word/long and
            // on-chip/cache-control targets are common; counting them as failures
            // would punish the more-correct behaviour.
            bool address_error_divergence = false;
            for (const auto& c : test.at("cycles")) {
                for (const char* key : {"read_addr", "write_addr"}) {
                    if (c.contains(key) &&
                        would_address_error(c.at(key).get<std::uint32_t>(), access_size)) {
                        address_error_divergence = true;
                    }
                }
            }
            if (address_error_divergence) {
                ++result.skipped;
                continue;
            }

            for (const auto& c : test.at("cycles")) {
                if (c.contains("read_addr")) {
                    put_be(bus, c.at("read_addr").get<std::uint32_t>(),
                           c.at("read_val").get<std::uint32_t>(), access_size);
                }
            }

            load_state(init, cpu);
            const std::uint32_t initial_sr = init.at("SR").get<std::uint32_t>();
            const int steps = step_count(op, initial_sr);
            for (int i = 0; i < steps; ++i) {
                cpu.step_instruction();
            }

            const auto r = cpu.cpu_registers();
            const auto& fin = test.at("final");
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
            for (const auto& c : test.at("cycles")) {
                if (!ok || !c.contains("write_addr")) {
                    continue;
                }
                const auto addr = c.at("write_addr").get<std::uint32_t>();
                const auto val = c.at("write_val").get<std::uint32_t>();
                const int wsize = access_size;
                std::uint32_t stored = 0;
                for (int b = 0; b < wsize; ++b) {
                    const auto it = bus.memory.find(addr + static_cast<std::uint32_t>(b));
                    stored = (stored << 8U) | (it == bus.memory.end() ? 0U : it->second);
                }
                const std::uint32_t expected =
                    wsize == 4 ? val : (val & ((1U << (8 * wsize)) - 1U));
                ok = stored == expected;
            }

            if (ok) {
                ++result.passed;
            } else {
                ++result.failed;
                if (failures.size() < 40U) {
                    failures.push_back(name);
                }
            }
        }
        return result;
    }

} // namespace

TEST_CASE("sh2 functional conformance against the SH4 single-step corpus") {
    const char* dir = std::getenv("MNEMOS_SH2_TESTS_DIR");
    if (dir == nullptr || std::string{dir}.empty() || !std::filesystem::is_directory(dir)) {
        SKIP("set MNEMOS_SH2_TESTS_DIR to a directory of transcoded SH4 single-step test JSON "
             "files (see THIRD-PARTY-REFERENCES.md)");
    }

    // A per-file cap keeps a smoke run fast; 0 (the default) runs every case.
    std::size_t max_tests = 0;
    if (const char* cap = std::getenv("MNEMOS_SH2_TESTS_MAX")) {
        max_tests = static_cast<std::size_t>(std::strtoul(cap, nullptr, 10));
    }

    file_result totals;
    std::vector<std::string> failures;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        ++files;
        const file_result fr = run_file(entry.path(), max_tests, failures);
        totals.passed += fr.passed;
        totals.failed += fr.failed;
        totals.skipped += fr.skipped;
    }

    INFO("SH-2 conformance: " << totals.passed << " passed, " << totals.failed << " failed, "
                              << totals.skipped << " skipped across " << files << " files");
    if (!failures.empty()) {
        for (const auto& f : failures) {
            std::cerr << "[sh2-conformance] FAIL " << f << "\n";
        }
    }
    REQUIRE(files > 0);
    CHECK(totals.failed == 0);
}
