#include "apm_plugin_abi.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {
    // A minimal Genesis cartridge that boots into an infinite self-branch touching
    // no RAM: reset SSP at $0, reset PC ($4) -> $200, and `bra.s *` (0x60 0xFE) at
    // $200. Enough for build_genesis_runtime to assemble and run a frame
    // deterministically (IRQs stay masked at reset, so nothing writes work_ram).
    std::vector<std::uint8_t> spin_rom() {
        std::vector<std::uint8_t> rom(0x400, 0x00);
        rom[0] = 0x00; // initial SSP = $00FF0000
        rom[1] = 0xFF;
        rom[2] = 0x00;
        rom[3] = 0x00;
        rom[4] = 0x00; // reset PC = $00000200
        rom[5] = 0x00;
        rom[6] = 0x02;
        rom[7] = 0x00;
        rom[0x200] = 0x60; // bra.s $200
        rom[0x201] = 0xFE;
        return rom;
    }
} // namespace

// Linked in-process (apm_plugin_entry is compiled into this test too), so this
// exercises the exact C ABI surface the tracer host drives over a loaded DLL.
extern "C" const apm_plugin_api* apm_plugin_entry(void);

TEST_CASE("genesis binding drives the engine through the C ABI") {
    const apm_plugin_api* api = apm_plugin_entry();
    REQUIRE(api != nullptr);
    CHECK(api->abi_version == APM_PLUGIN_ABI_VERSION);
    CHECK(std::string(api->system_name) == "genesis");

    apm_plugin* p = api->create();
    REQUIRE(p != nullptr);

    const std::vector<std::uint8_t> rom = spin_rom();
    REQUIRE(api->load_rom(p, rom.data(), rom.size()) == 0);

    SECTION("work_ram bank is exposed with the right shape") {
        REQUIRE(api->bank_count(p) >= 1);
        apm_bank_info info{};
        REQUIRE(api->get_bank(p, 0, &info) == 0);
        CHECK(info.host_ptr != nullptr);
        CHECK(info.size == 0x10000U);
        CHECK(info.guest_base == 0xFF0000U);
        CHECK(std::string(info.name) == "work_ram");
    }

    SECTION("run_frame advances the frame index") {
        CHECK(api->frame_index(p) == 0U);
        CHECK(api->run_frame(p) == 1U);
        CHECK(api->frame_index(p) == 1U);
    }

    SECTION("read_register reports the spinning instruction address") {
        api->run_frame(p);
        CHECK(api->read_register(p, APM_REG_INST) == 0x200U);
    }

    api->destroy(p);
}
