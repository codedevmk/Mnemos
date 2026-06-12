// Z80 conformance via a public CP/M instruction-exerciser .com image.
//
// The community-standard Z80 exercisers (see THIRD-PARTY-REFERENCES.md) are CP/M .com
// programs that run every Z80 instruction against thousands of operand
// combinations and CRC-check the results. This harness runs one in a minimal
// CP/M environment and asserts it reports no errors.
//
// The .com images are not committed, so this test is DATA-GATED: it SKIPs unless
// MNEMOS_Z80_TEST_ROM points at one.
//
//   MNEMOS_Z80_TEST_ROM        path to the .com exerciser image
//   MNEMOS_Z80_TEST_MAX_INSTR  (optional) instruction-count safety cap
//                              (default 20e9; full-flag exercisers need ~11e9)
//
// CP/M contract the exercisers rely on: the program is the Transient Program Area
// at $0100; console output is BDOS function 2 (char in E) / 9 ($-string at DE)
// reached by CALL $0005; the program ends by jumping to the $0000 warm-boot entry.
// We trap PC==$0005 to emulate the two BDOS calls (a RET sits there) and PC==$0000
// to detect completion.

#include "z80.hpp"

#include "bus.hpp"
#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::chips::cpu::z80;
    using reset_kind = mnemos::chips::reset_kind;

    std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
        char* buf = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
            return std::nullopt;
        }
        std::string value(buf);
        std::free(buf);
        return value;
#else
        const char* value = std::getenv(name);
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string(value);
#endif
    }
} // namespace

TEST_CASE("z80 passes the public CP/M instruction exerciser", "[conformance][z80]") {
    const auto rom = get_env("MNEMOS_Z80_TEST_ROM");
    if (!rom) {
        SKIP("set MNEMOS_Z80_TEST_ROM to a CP/M .com exerciser image (not committed; "
             "see THIRD-PARTY-REFERENCES.md)");
    }
    auto image = mnemos::io::read_file(fs::path(*rom).string());
    if (!image || image->empty() || image->size() > 0xFF00U) {
        SKIP("MNEMOS_Z80_TEST_ROM=" << *rom
                                    << " could not be read as a CP/M .com (<= 0xFF00 bytes)");
    }

    std::uint64_t max_instr = 20'000'000'000ULL;
    if (const auto cap = get_env("MNEMOS_Z80_TEST_MAX_INSTR")) {
        const std::uint64_t parsed = std::strtoull(cap->c_str(), nullptr, 10);
        if (parsed != 0U) {
            max_instr = parsed;
        }
    }

    // 64 KiB address space; load the exerciser at the CP/M TPA ($0100). Put a RET at
    // the BDOS entry ($0005) so the CALL returns cleanly after we emulate it.
    auto ram = std::make_unique<std::array<std::uint8_t, 0x10000>>();
    ram->fill(0U);
    std::copy(image->begin(), image->end(), ram->begin() + 0x0100);
    (*ram)[0x0005] = 0xC9U; // RET

    mnemos::topology::bus bus(16U, mnemos::topology::endianness::little);
    bus.map_ram(0x0000U, std::span<std::uint8_t>(*ram), 0);

    z80 cpu;
    cpu.attach_bus(bus);
    cpu.reset(reset_kind::power_on);
    auto regs = cpu.cpu_registers();
    regs.pc = 0x0100U;
    regs.sp = 0xF000U;
    cpu.set_registers(regs);

    std::string output;
    bool completed = false;
    for (std::uint64_t n = 0; n < max_instr; ++n) {
        const auto r = cpu.cpu_registers();
        if (r.pc == 0x0000U) { // jump to the warm-boot entry -> done
            completed = true;
            break;
        }
        if (r.halted) { // the exercisers never HALT; treat it as a stop
            break;
        }
        if (r.pc == 0x0005U) { // BDOS console call
            const auto fn = static_cast<std::uint8_t>(r.bc & 0xFFU);
            if (fn == 2U) {
                output.push_back(static_cast<char>(r.de & 0xFFU)); // print E
            } else if (fn == 9U) {
                std::uint16_t addr = r.de; // print $-terminated string at DE
                for (int i = 0; i < 0x10000; ++i) {
                    const std::uint8_t ch = (*ram)[addr++];
                    if (ch == static_cast<std::uint8_t>('$')) {
                        break;
                    }
                    output.push_back(static_cast<char>(ch));
                }
            }
        }
        cpu.step_instruction();
    }

    INFO("exerciser output:\n" << output);
    REQUIRE(completed); // reached the warm-boot exit within the instruction cap
    CHECK_FALSE(output.empty());
    // Each sub-test prints "OK" on success and "ERROR" (with the CRCs) on failure.
    CHECK(output.find("ERROR") == std::string::npos);
}
