#include "bank_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {
    using mnemos::apm::memory::bank_registry;
    using mnemos::apm::memory::memory_tag;
    using mnemos::apm::memory::tagged_bank;

    constexpr std::uint32_t chip_cpu = 1;
    constexpr std::uint32_t chip_vdp = 3;
} // namespace

TEST_CASE("bank_registry resolves a host address inside a bank to its tag and guest address") {
    bank_registry reg;
    std::array<std::uint8_t, 0x10000> work_ram{}; // stand-in backing
    reg.add(memory_tag{chip_cpu, 0, 0xFF0000}, work_ram.data(), work_ram.size());

    const tagged_bank* bank = reg.find_by_address(work_ram.data() + 0x809);
    REQUIRE(bank != nullptr);
    CHECK(bank->tag.chip == chip_cpu);
    CHECK(bank->tag.bank == 0);
    CHECK(bank->tag.guest_base == 0xFF0000);
    CHECK(bank->host_ptr == work_ram.data());
    CHECK(bank->size == work_ram.size());
    CHECK(bank->guest_address_of(work_ram.data() + 0x809) == 0xFF0809); // $FFF809 mapping
}

TEST_CASE("bank_registry returns nullptr for an address outside every bank") {
    bank_registry reg;
    std::array<std::uint8_t, 0x100> bank0{};
    std::array<std::uint8_t, 0x100> outside{};
    reg.add(memory_tag{chip_cpu, 0, 0}, bank0.data(), bank0.size());

    CHECK(reg.find_by_address(outside.data()) == nullptr);
    CHECK(reg.find_by_address(bank0.data() + bank0.size()) == nullptr); // one past the end
}

TEST_CASE("bank_registry finds a bank by (chip, bank) identity") {
    bank_registry reg;
    std::array<std::uint8_t, 0x100> ram{};
    std::array<std::uint8_t, 0x4000> vram{};
    reg.add(memory_tag{chip_cpu, 0, 0xFF0000}, ram.data(), ram.size());
    reg.add(memory_tag{chip_vdp, 0, 0}, vram.data(), vram.size());

    const tagged_bank* v = reg.find_by_tag(chip_vdp, 0);
    REQUIRE(v != nullptr);
    CHECK(v->host_ptr == vram.data());
    CHECK(reg.find_by_tag(chip_vdp, 7) == nullptr); // no such bank
}

TEST_CASE("bank_registry forgets a removed bank") {
    bank_registry reg;
    std::array<std::uint8_t, 0x100> ram{};
    reg.add(memory_tag{chip_cpu, 0, 0}, ram.data(), ram.size());
    REQUIRE(reg.find_by_address(ram.data()) != nullptr);

    reg.remove(ram.data());
    CHECK(reg.find_by_address(ram.data()) == nullptr);
    CHECK(reg.banks().empty());
}
