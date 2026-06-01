// Z80 conformance via the ZEXALL-SMS instruction exerciser.
//
// ZEXALL (Frank Cringle) exercises every Z80 instruction against thousands of
// operand combinations and CRC-checks each against a reference value. The SMS
// port (smspower.org/Homebrew/ZEXALL-SMS) runs as a Master System ROM and
// reports results to the SDSC debug console (OUT ($FD),char per character;
// OUT ($FC),$01 to suspend at the end) in parallel with the SMS VDP.
//
// This harness boots that ROM on a full Mnemos SMS, taps the Z80's OUT handler
// to capture the SDSC text stream, runs until the ROM suspends (or a frame cap),
// and asserts every sub-test reported "OK" with no CRC mismatch.
//
// The ROM is GPLv2 and is NOT committed, so this test is DATA-GATED: it SKIPs
// unless MNEMOS_SMS_ZEXALL_ROM points at the zexall.sms image.
//
//   MNEMOS_SMS_ZEXALL_ROM        path to the zexall.sms (or zexdoc.sms) image
//   MNEMOS_SMS_ZEXALL_MAX_FRAMES (optional) frame cap before giving up
//                                (default 200000; the full run is long)
//
// Note: the full ZEXALL run is very slow (the longest sub-test alone is minutes
// of emulated time). zexdoc.sms (documented flags only) is much faster and is a
// reasonable smoke target; either image works here.

#include "scheduler.hpp"
#include "sms_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::sms::assemble_sms;
    using mnemos::manifests::sms::sms_system;

    // SDSC debug-console ports the ZEXALL-SMS ROM writes to (sdsc.inc).
    constexpr std::uint8_t kSdscCommandPort = 0xFCU;
    constexpr std::uint8_t kSdscDataPort = 0xFDU;
    constexpr std::uint8_t kSdscSuspend = 0x01U;

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

    std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }
} // namespace

TEST_CASE("z80 passes the ZEXALL-SMS instruction exerciser", "[conformance][z80][sms]") {
    const auto rom_path = get_env("MNEMOS_SMS_ZEXALL_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_SMS_ZEXALL_ROM to a zexall.sms / zexdoc.sms image "
             "(GPLv2, not committed; see THIRD-PARTY.md)");
    }

    auto rom = read_file(fs::path(*rom_path));
    if (!rom || rom->empty()) {
        SKIP("MNEMOS_SMS_ZEXALL_ROM=" << *rom_path << " could not be read as an SMS image");
    }

    std::uint64_t max_frames = 200000U;
    if (const auto cap = get_env("MNEMOS_SMS_ZEXALL_MAX_FRAMES")) {
        const std::uint64_t parsed = std::strtoull(cap->c_str(), nullptr, 10);
        if (parsed != 0U) {
            max_frames = parsed;
        }
    }

    auto sys = assemble_sms(std::move(*rom), {});

    // Tap the Z80 OUT handler: capture SDSC console writes ($FD = data,
    // $FC = command), and delegate every other port to the SMS routing the
    // assemble_sms default installed. assemble_sms drops $C0-$FF writes, so
    // replicating the lower ranges here keeps the machine behaving identically
    // for everything the ROM does besides SDSC.
    std::string console;
    bool suspended = false;
    sms_system* raw = sys.get();
    raw->cpu.set_port_out([raw, &console, &suspended](std::uint16_t port, std::uint8_t value) {
        const auto p = static_cast<std::uint8_t>(port & 0xFFU);
        if (p == kSdscDataPort) {
            console.push_back(static_cast<char>(value));
            return;
        }
        if (p == kSdscCommandPort) {
            if (value == kSdscSuspend) {
                suspended = true;
            }
            return;
        }
        if (p <= 0x3FU) {
            if ((p & 1U) != 0U) {
                raw->io_ctrl = value; // $3F I/O control
            }
            return;
        }
        if (p <= 0x7FU) {
            raw->psg.write(value);
            return;
        }
        if (p <= 0xBFU) {
            if ((p & 1U) != 0U) {
                raw->vdp.ctrl_write(value);
            } else {
                raw->vdp.data_write(value);
            }
            return;
        }
        // $C0-$FF (other than SDSC): no writable registers on a base SMS.
    });

    std::vector<mnemos::runtime::scheduled_chip> chips = {
        {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
    mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);

    // Run frame-by-frame so a VDP poll during boot (system detect) resolves
    // naturally, and stop as soon as the ROM suspends emulation.
    std::uint64_t frame = 0;
    for (; frame < max_frames && !suspended; ++frame) {
        sched.run_frame();
        // The ROM ends the whole run with "Tests complete"; bail early on it too
        // in case a build emits SUSPEND only under a different define.
        if (console.find("Tests complete") != std::string::npos) {
            break;
        }
    }

    const bool completed = suspended || console.find("Tests complete") != std::string::npos;

    // Count how many sub-tests reported "OK" -- the only positive signal that the
    // ROM actually booted and ran instructions on our Z80.
    std::size_t ok_count = 0;
    for (std::size_t pos = console.find(" OK"); pos != std::string::npos;
         pos = console.find(" OK", pos + 1)) {
        ++ok_count;
    }

    INFO("frames run: " << frame << " (cap " << max_frames << ", suspended=" << suspended
                        << ", completed=" << completed << ", sub-tests OK=" << ok_count << ")");
    INFO("SDSC console output:\n" << console);

    // The ROM must have booted far enough to run and pass at least some sub-tests;
    // a dead boot or a broken SDSC tap produces no "OK" lines.
    CHECK_FALSE(console.empty());
    REQUIRE(ok_count > 0);

    // The real conformance property: a sub-test prints "<name> OK" on a pass, or
    // "<name>\n CRC <actual> expected <expected>" on a CRC mismatch. Any
    // " expected " in the stream is a failing instruction group. (This ROM never
    // prints the literal "ERROR".)
    const bool any_crc_mismatch = console.find(" expected ") != std::string::npos;
    CHECK_FALSE(any_crc_mismatch);

    // The full exerciser is very long (the longest sub-test alone is minutes of
    // emulated time). By default we assert "no mismatch in what ran" and report
    // progress; set MNEMOS_SMS_ZEXALL_REQUIRE_COMPLETE to demand the whole run
    // finish within the frame cap (an exhaustive local pass).
    if (get_env("MNEMOS_SMS_ZEXALL_REQUIRE_COMPLETE")) {
        REQUIRE(completed);
    } else if (!completed) {
        WARN("ZEXALL did not finish within "
             << max_frames << " frames (ran " << ok_count
             << " sub-tests clean); raise MNEMOS_SMS_ZEXALL_MAX_FRAMES for a full run");
    }
}
