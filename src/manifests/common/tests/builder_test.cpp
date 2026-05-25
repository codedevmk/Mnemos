#include "builder.hpp"

#include "manifest.hpp"

#include "cia_6526.hpp"
#include "m6510.hpp"
#include "sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
    using namespace mnemos::manifests;

    // A ROM provider that yields nothing.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> no_roms(std::string_view) {
        return std::nullopt;
    }
} // namespace

TEST_CASE("build_system attaches a CPU to its bus and runs from RAM") {
    const std::string text = R"toml(
[manifest]
schema = "mnemos-manifest/1"
id = "test.cpu"
[clock]
master_hz = 1000000
[[chip]]
id = "cpu"
type = "mos.6510"
attached_bus = "main"
[[bus]]
id = "main"
address_bits = 16
endianness = "little"
[[bus.region]]
name = "ram"
range = "0x0000-0xFFFF"
backing = "ram"
size = 65536
)toml";

    const auto parsed = parse_manifest(text);
    REQUIRE(parsed.ok());
    auto built = build_system(*parsed.value, no_roms);
    REQUIRE(built.ok());

    auto& graph = *built.value;
    auto* bus = graph.bus("main");
    REQUIRE(bus != nullptr);
    auto* cpu = dynamic_cast<mnemos::chips::cpu::m6510*>(graph.chip("cpu"));
    REQUIRE(cpu != nullptr);

    bus->write8(0xC000U, 0xEAU); // NOP
    bus->write8(0xFFFCU, 0x00U); // reset vector -> $C000
    bus->write8(0xFFFDU, 0xC0U);
    cpu->reset(mnemos::chips::reset_kind::power_on);
    REQUIRE(cpu->cpu_registers().pc == 0xC000U); // vector read through the attached bus

    cpu->tick(2U); // execute the NOP
    CHECK(cpu->cpu_registers().pc == 0xC001U);
}

TEST_CASE("build_system loads a verified ROM and binds an MMIO chip") {
    // Touch a non-inline CIA symbol so this TU pulls in the cia_6526 object and
    // its static-init factory registration runs (so create_chip("mos.6526")
    // resolves). The runtime CLI will instead whole-archive-link the chip set.
    [[maybe_unused]] const mnemos::chips::bus_controller::cia_6526 force_cia_link{};

    const std::vector<std::uint8_t> rom = {0x11U, 0x22U, 0x33U, 0x44U};
    const std::string sha = mnemos::foundation::sha256(std::span<const std::uint8_t>(rom)).hex();

    const std::string text = R"toml(
[manifest]
schema = "mnemos-manifest/1"
id = "test.machine"
[clock]
master_hz = 1000000
[[chip]]
id = "cia1"
type = "mos.6526"
attached_bus = "main"
mmio_range = "0xDC00-0xDCFF"
[[bus]]
id = "main"
address_bits = 16
endianness = "little"
[[bus.region]]
name = "ram"
range = "0x0000-0x7FFF"
backing = "ram"
size = 32768
[[bus.region]]
name = "test_rom"
range = "0x8000-0x8003"
backing = "rom"
file = "test.rom"
sha256 = ")toml" + sha + R"toml("
)toml";

    const auto parsed = parse_manifest(text);
    REQUIRE(parsed.ok());
    auto built = build_system(*parsed.value, [&](std::string_view f) {
        return f == "test.rom" ? std::optional{rom} : std::nullopt;
    });
    REQUIRE(built.ok());

    auto* bus = built.value->bus("main");
    REQUIRE(bus != nullptr);
    // The CIA was created by factory id and is the right concrete type.
    CHECK(dynamic_cast<mnemos::chips::bus_controller::cia_6526*>(built.value->chip("cia1")) !=
          nullptr);

    bus->write8(0x0010U, 0xABU); // RAM
    CHECK(bus->read8(0x0010U) == 0xABU);

    CHECK(bus->read8(0x8000U) == 0x11U); // ROM
    CHECK(bus->read8(0x8003U) == 0x44U);
    bus->write8(0x8000U, 0xFFU);
    CHECK(bus->read8(0x8000U) == 0x11U); // ROM ignores writes

    bus->write8(0xDC02U, 0x5AU);         // CIA1 DDRA via MMIO routing
    CHECK(bus->read8(0xDC02U) == 0x5AU); // reads back through the chip
}

TEST_CASE("build_system reports an unknown chip type") {
    const std::string text = R"toml(
[manifest]
schema = "mnemos-manifest/1"
id = "x"
[clock]
master_hz = 1
[[chip]]
id = "mystery"
type = "acme.9999"
attached_bus = "main"
[[bus]]
id = "main"
)toml";
    const auto parsed = parse_manifest(text);
    REQUIRE(parsed.ok());
    const auto built = build_system(*parsed.value, no_roms);
    CHECK_FALSE(built.ok());
    bool found = false;
    for (const auto& d : built.errors) {
        if (d.message.find("unknown or unregistered type") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("build_system rejects a ROM whose SHA-256 does not match") {
    const std::string text = R"toml(
[manifest]
schema = "mnemos-manifest/1"
id = "x"
[clock]
master_hz = 1
[[bus]]
id = "main"
[[bus.region]]
name = "rom"
range = "0x8000-0x8003"
backing = "rom"
file = "r.bin"
sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
)toml";
    const auto parsed = parse_manifest(text);
    REQUIRE(parsed.ok());
    const std::vector<std::uint8_t> bytes = {0x01U, 0x02U, 0x03U, 0x04U};
    const auto built = build_system(*parsed.value, [&](std::string_view) { return bytes; });
    CHECK_FALSE(built.ok());
    bool found = false;
    for (const auto& d : built.errors) {
        if (d.message.find("sha256 mismatch") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}
