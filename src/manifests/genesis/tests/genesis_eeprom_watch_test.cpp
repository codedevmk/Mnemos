// Diagnostic harness: trace 68K accesses to the cartridge serial-EEPROM port
// window ($200000-$2000FF) on the real (build_genesis_runtime) path, to reverse-
// engineer a cart's I2C pin mapping (which bit is SCL, SDA-write, SDA-read).
//
// Data-gated. Set:
//   MNEMOS_GENESIS_ROM           cartridge image (an EEPROM cart, e.g. NBA Jam)
//   MNEMOS_EEPROM_WATCH_FRAMES   frames to run (decimal, default 4)
//   MNEMOS_EEPROM_WATCH_BASE     port base (hex, default 200000)
// Each access in [base, base+0xFF] logs "[ee] R/W $AAAAAA = VV (bits b7..b0)".
// SKIPs cleanly when MNEMOS_GENESIS_ROM is unset.

#include "genesis_runtime.hpp"

#include "bus.hpp"
#include "eeprom_i2c.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
    using mnemos::manifests::genesis::build_genesis_runtime;
    using mnemos::manifests::genesis::genesis_config;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    std::optional<std::string> env(const char* n) {
        const char* v = std::getenv(n);
        if (v == nullptr || v[0] == '\0') {
            return std::nullopt;
        }
        return std::string(v);
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace

TEST_CASE("genesis EEPROM port watch (diagnostic)", "[genesis][diag]") {
    const auto rom_path = env("MNEMOS_GENESIS_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_GENESIS_ROM to trace cartridge EEPROM-port accesses");
    }
    std::ifstream in(*rom_path, std::ios::binary);
    REQUIRE(in);
    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>{});
    REQUIRE_FALSE(rom.empty());

    const std::uint32_t base = env("MNEMOS_EEPROM_WATCH_BASE")
                                   ? static_cast<std::uint32_t>(std::strtoul(
                                         env("MNEMOS_EEPROM_WATCH_BASE")->c_str(), nullptr, 16))
                                   : 0x200000U;
    const int frames = env("MNEMOS_EEPROM_WATCH_FRAMES")
                           ? std::atoi(env("MNEMOS_EEPROM_WATCH_FRAMES")->c_str())
                           : 4;

    auto rt = build_genesis_runtime(rom, genesis_config{});

    // Optional device-in-the-loop: wire a real eeprom_i2c at the port with the
    // candidate Acclaim pin mapping (SDA-out = base.bit0, SCL = base+1.bit0,
    // SDA-in = base+1.bit0). Priority 2 so it overrides ROM/SRAM at the two port
    // bytes. With a responding device the cart should run a full clean handshake
    // (ACKs land, reads return real data) instead of aborting on open bus.
    std::shared_ptr<mnemos::chips::storage::eeprom_i2c> eeprom;
    if (env("MNEMOS_EEPROM_WIRE")) {
        const std::size_t size =
            env("MNEMOS_EEPROM_SIZE")
                ? static_cast<std::size_t>(std::strtoul(env("MNEMOS_EEPROM_SIZE")->c_str(), nullptr,
                                                        10))
                : 2048U; // 24C16 (Acclaim 2ME default)
        // Configurable pin mapping so the candidate can be brute-forced without a
        // rebuild. Offsets are 0 ($200000) or 1 ($200001); bits default to 0 (the
        // trace only ever moves bit 0). Defaults = candidate A.
        const auto off = [](const char* n, unsigned d) {
            return env(n) ? static_cast<unsigned>(std::strtoul(env(n)->c_str(), nullptr, 10)) : d;
        };
        const unsigned sda_w_off = off("MNEMOS_EEPROM_SDAW", 0U); // SDA-write byte offset
        const unsigned scl_off = off("MNEMOS_EEPROM_SCL", 1U);    // SCL byte offset
        const unsigned sda_r_off = off("MNEMOS_EEPROM_SDAR", 1U); // SDA-read byte offset
        const unsigned sda_bit = off("MNEMOS_EEPROM_BIT", 0U);

        eeprom = std::make_shared<mnemos::chips::storage::eeprom_i2c>(size);
        auto scl = std::make_shared<bool>(true);
        auto sda = std::make_shared<bool>(true);
        rt->state.main_bus->map_mmio(
            base, 2U,
            [eeprom, base, sda_r_off, sda_bit](std::uint32_t a) -> std::uint8_t {
                if ((a - base) == sda_r_off) {
                    return static_cast<std::uint8_t>(eeprom->sda() ? (1U << sda_bit) : 0U);
                }
                return 0xFFU;
            },
            [eeprom, scl, sda, base, sda_w_off, scl_off, sda_bit](std::uint32_t a, std::uint8_t v) {
                const unsigned o = a - base;
                if (o == sda_w_off) {
                    *sda = (v & (1U << sda_bit)) != 0U;
                }
                if (o == scl_off) {
                    *scl = (v & (1U << sda_bit)) != 0U;
                }
                eeprom->update(*scl, *sda);
            },
            /*priority=*/2);
        std::fprintf(stderr,
                     "[ee] device-in-the-loop: 24Cxx %zu B  SDAw=+%u SCL=+%u SDAr=+%u bit%u\n",
                     size, sda_w_off, scl_off, sda_r_off, sda_bit);
    }

    auto* cpu = rt->cpu();
    rt->state.main_bus->set_access_observer([base, cpu](const mnemos::topology::access_event& ev) {
        const std::uint32_t a = ev.address & 0xFFFFFFU;
        // Above-ROM cart region (EEPROM/SRAM port) plus the $A130xx mapper /
        // time-register band. For a >=2 MB cart [base, $3FFFFF] is all non-ROM,
        // so this does not flood with ROM fetches.
        const bool cart = a >= base && a <= 0x3FFFFFU;
        const bool mapper = a >= 0xA13000U && a <= 0xA130FFU;
        if (!cart && !mapper) {
            return;
        }
        char bits[9];
        for (int i = 0; i < 8; ++i) {
            bits[i] = ((ev.value >> (7 - i)) & 1U) ? '1' : '0';
        }
        bits[8] = '\0';
        const auto pc = cpu->current_instruction_addr() & 0xFFFFFFU;
        std::fprintf(stderr, "[ee] pc=$%06X  %s $%06X = %02X  %s\n", static_cast<unsigned>(pc),
                     ev.write ? "W" : "R", static_cast<unsigned>(a),
                     static_cast<unsigned>(ev.value), bits);
    });

    std::vector<mnemos::runtime::scheduled_chip> chips;
    for (const auto& e : rt->schedule()) {
        chips.push_back({e.chip, e.weight});
    }
    mnemos::runtime::scheduler sched(std::move(chips), rt->vdp());
    for (int i = 0; i < frames; ++i) {
        std::fprintf(stderr, "=== frame %d ===\n", i + 1);
        sched.run_frame();
    }
    SUCCEED();
}
