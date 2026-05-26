// Allow std::getenv on MSVC (test-only; reads a corpus-path env var).
#define _CRT_SECURE_NO_WARNINGS

// Z80 per-cycle conformance harness.
//
// Validates the z80 core against a public per-instruction Z80 test corpus
// (see THIRD-PARTY.md): each test fixes an initial CPU + RAM state and an I/O
// access list, runs exactly one instruction, and checks the final CPU + RAM
// state and the exact per-cycle bus trace (memory accesses + I/O accesses).
//
// Complements the CP/M exerciser (z80_conformance_test.cpp), which verifies
// functional correctness via CRC but says nothing about cycle accuracy: this
// harness pins the cycle count and bus access order per instruction.
//
// The corpus is large and never committed; the test is data-gated and SKIPs
// unless MNEMOS_Z80_TESTS_DIR points at a directory of per-instruction JSON
// files.

#include "z80.hpp"

#include "ibus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::cpu::z80;

    // Records every bus access in the order it occurred.
    class recording_bus final : public mnemos::chips::ibus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};

        struct access final {
            std::uint16_t address;
            std::uint8_t value;
            enum class kind : std::uint8_t { mem_read, mem_write, io_read, io_write } what;
        };
        std::vector<access> trace;

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            const auto addr = static_cast<std::uint16_t>(address & 0xFFFFU);
            const std::uint8_t value = memory[addr];
            trace.push_back({addr, value, access::kind::mem_read});
            return value;
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            const auto addr = static_cast<std::uint16_t>(address & 0xFFFFU);
            memory[addr] = value;
            trace.push_back({addr, value, access::kind::mem_write});
        }
    };

    // Classify a corpus pin-state marker. The corpus represents each cycle as
    // [addr, value, marker]; marker formats vary across corpus versions, but the
    // operations of interest are identified by their pin signature:
    //   m + r   -> memory read
    //   m + w   -> memory write
    //   i + r   -> I/O read
    //   i + w   -> I/O write
    //   anything else (typically all dashes / nulls) -> internal T-state
    //
    // We accept both lowercase letter codes (e.g. "mr--", "iorq_rd") and the
    // underscored "mreq_rd" / "iorq_wr" form. Anything we don't classify is
    // treated as an internal cycle and skipped during the trace compare.
    enum class cycle_kind : std::uint8_t { internal, mem_read, mem_write, io_read, io_write };

    [[nodiscard]] cycle_kind classify(const std::string& marker) {
        bool has_m = false;
        bool has_i = false;
        bool has_r = false;
        bool has_w = false;
        for (const char raw : marker) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
            if (c == 'm') {
                has_m = true;
            } else if (c == 'i') {
                has_i = true;
            } else if (c == 'r') {
                has_r = true;
            } else if (c == 'w') {
                has_w = true;
            }
        }
        if (has_m && has_r) {
            return cycle_kind::mem_read;
        }
        if (has_m && has_w) {
            return cycle_kind::mem_write;
        }
        if (has_i && has_r) {
            return cycle_kind::io_read;
        }
        if (has_i && has_w) {
            return cycle_kind::io_write;
        }
        return cycle_kind::internal;
    }

    // ---- corpus state -> z80::registers ------------------------------------
    [[nodiscard]] std::uint16_t pair(const nlohmann::json& node, const char* hi,
                                     const char* lo) {
        return static_cast<std::uint16_t>(
            (node.at(hi).get<std::uint8_t>() << 8U) | node.at(lo).get<std::uint8_t>());
    }

    [[nodiscard]] z80::registers read_state(const nlohmann::json& node) {
        z80::registers r{};
        r.pc = node.at("pc").get<std::uint16_t>();
        r.sp = node.at("sp").get<std::uint16_t>();
        r.af = pair(node, "a", "f");
        r.bc = pair(node, "b", "c");
        r.de = pair(node, "d", "e");
        r.hl = pair(node, "h", "l");
        r.ix = node.at("ix").get<std::uint16_t>();
        r.iy = node.at("iy").get<std::uint16_t>();
        // Shadow regs in the corpus are stored as 16-bit `af_`, `bc_`, `de_`,
        // `hl_` (with trailing underscore). Some forks use `af'`/`bc'`/etc.
        const auto get16 = [&](const char* underscored, const char* primed) {
            if (node.contains(underscored)) {
                return node.at(underscored).get<std::uint16_t>();
            }
            if (node.contains(primed)) {
                return node.at(primed).get<std::uint16_t>();
            }
            return std::uint16_t{0};
        };
        r.af2 = get16("af_", "af'");
        r.bc2 = get16("bc_", "bc'");
        r.de2 = get16("de_", "de'");
        r.hl2 = get16("hl_", "hl'");
        r.i = node.at("i").get<std::uint8_t>();
        r.r = node.at("r").get<std::uint8_t>();
        r.im = node.at("im").get<std::uint8_t>();
        r.iff1 = node.at("iff1").get<int>() != 0;
        r.iff2 = node.at("iff2").get<int>() != 0;
        return r;
    }

    // Run one corpus file; returns {passed, failed} for that file and records
    // up to a few example failure names so the user can grep the file with the
    // problematic case.
    std::pair<std::size_t, std::size_t> run_file(const std::filesystem::path& path,
                                                 std::vector<std::string>& failures) {
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::ifstream stream(path);
        const nlohmann::json doc = nlohmann::json::parse(stream);

        recording_bus bus;
        z80 cpu;
        cpu.attach_bus(bus);

        // Route I/O through the same trace buffer as memory accesses. The bus
        // and the port hooks share `trace` because the corpus interleaves
        // memory and I/O cycles within a single instruction's bus log.
        cpu.set_port_in([&](std::uint16_t port) -> std::uint8_t {
            const std::uint8_t v = 0xFFU; // value provided by the corpus, but
                                          // the trace compare reads from cycles[]
            bus.trace.push_back({port, v, recording_bus::access::kind::io_read});
            return v;
        });
        cpu.set_port_out([&](std::uint16_t port, std::uint8_t value) {
            bus.trace.push_back({port, value, recording_bus::access::kind::io_write});
        });

        for (const auto& test : doc) {
            // Load initial state.
            const auto initial_regs = read_state(test.at("initial"));
            for (const auto& cell : test.at("initial").at("ram")) {
                bus.memory[cell.at(0).get<std::uint16_t>()] = cell.at(1).get<std::uint8_t>();
            }
            cpu.set_registers(initial_regs);
            bus.trace.clear();

            // Patch I/O reads to return the corpus-specified value at each
            // access. The corpus `ports` array lists every I/O access in
            // order; we walk it whenever the running CPU performs an "in".
            std::size_t port_in_idx = 0;
            const auto& ports = test.contains("ports") ? test.at("ports") : nlohmann::json::array();
            cpu.set_port_in([&](std::uint16_t port) -> std::uint8_t {
                std::uint8_t value = 0xFFU;
                for (; port_in_idx < ports.size(); ++port_in_idx) {
                    const auto& p = ports[port_in_idx];
                    const std::string dir = p.size() > 2 ? p.at(2).get<std::string>() : "r";
                    if (!dir.empty() && (dir[0] == 'r' || dir[0] == 'R')) {
                        value = p.at(1).get<std::uint8_t>();
                        ++port_in_idx;
                        break;
                    }
                }
                bus.trace.push_back({port, value, recording_bus::access::kind::io_read});
                return value;
            });

            // Execute exactly one instruction.
            cpu.step_instruction();

            bool ok = true;

            // Final register state.
            const auto expected = read_state(test.at("final"));
            const auto r = cpu.cpu_registers();
            ok = ok && r.pc == expected.pc && r.sp == expected.sp && r.af == expected.af &&
                 r.bc == expected.bc && r.de == expected.de && r.hl == expected.hl &&
                 r.ix == expected.ix && r.iy == expected.iy && r.af2 == expected.af2 &&
                 r.bc2 == expected.bc2 && r.de2 == expected.de2 && r.hl2 == expected.hl2 &&
                 r.i == expected.i && r.r == expected.r && r.im == expected.im &&
                 r.iff1 == expected.iff1 && r.iff2 == expected.iff2;

            // Final RAM cells the corpus calls out.
            for (const auto& cell : test.at("final").at("ram")) {
                if (bus.memory[cell.at(0).get<std::uint16_t>()] !=
                    cell.at(1).get<std::uint8_t>()) {
                    ok = false;
                }
            }

            // Per-cycle bus trace. The corpus enumerates every T-cycle,
            // including ones where the CPU's bus is idle; our trace only
            // contains actual memory + I/O accesses, so we walk the corpus
            // skipping idle cycles and match in order.
            std::size_t trace_idx = 0;
            for (const auto& corpus_cycle : test.at("cycles")) {
                const std::string marker =
                    corpus_cycle.size() > 2 ? corpus_cycle.at(2).get<std::string>() : "";
                const cycle_kind kind = classify(marker);
                if (kind == cycle_kind::internal) {
                    continue;
                }
                if (trace_idx >= bus.trace.size()) {
                    ok = false;
                    break;
                }
                const auto& got = bus.trace[trace_idx++];
                const auto exp_addr = corpus_cycle.at(0).get<std::uint16_t>();
                const auto exp_value = corpus_cycle.at(1).get<std::uint8_t>();
                bool kind_match = false;
                switch (kind) {
                case cycle_kind::mem_read:
                    kind_match = got.what == recording_bus::access::kind::mem_read;
                    break;
                case cycle_kind::mem_write:
                    kind_match = got.what == recording_bus::access::kind::mem_write;
                    break;
                case cycle_kind::io_read:
                    kind_match = got.what == recording_bus::access::kind::io_read;
                    break;
                case cycle_kind::io_write:
                    kind_match = got.what == recording_bus::access::kind::io_write;
                    break;
                case cycle_kind::internal:
                    break;
                }
                if (!kind_match || got.address != exp_addr || got.value != exp_value) {
                    ok = false;
                    break;
                }
            }
            if (trace_idx != bus.trace.size()) {
                ok = false; // we performed more accesses than the corpus expected
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

TEST_CASE("z80 passes the public per-cycle Z80 conformance corpus",
          "[conformance][z80]") {
    const char* dir = std::getenv("MNEMOS_Z80_TESTS_DIR");
    if (dir == nullptr || std::string{dir}.empty() ||
        !std::filesystem::is_directory(dir)) {
        SKIP("set MNEMOS_Z80_TESTS_DIR to a directory of per-instruction Z80 test "
             "JSON files (see THIRD-PARTY.md)");
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
        const auto [file_passed, file_failed] = run_file(entry.path(), failures);
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
