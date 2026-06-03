#include "tagged_allocator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {
    using mnemos::apm::memory::memory_tag;
    using mnemos::apm::memory::tagged_allocator;
    using mnemos::apm::memory::tagged_bank;

    constexpr std::uint32_t chip_cpu = 1;
} // namespace

TEST_CASE("tagged_allocator hands out a page-aligned, writable, registered bank") {
    tagged_allocator alloc;
    auto* mem = static_cast<std::uint8_t*>(
        alloc.allocate_bank(memory_tag{chip_cpu, 0, 0xFF0000}, 0x10000));

    REQUIRE(mem != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(mem) % 4096U == 0U); // page-aligned

    mem[0x809] = 0x42U; // writable
    CHECK(mem[0x809] == 0x42U);

    const tagged_bank* bank = alloc.registry().find_by_address(mem + 0x809);
    REQUIRE(bank != nullptr);
    CHECK(bank->tag.chip == chip_cpu);
    CHECK(bank->tag.guest_base == 0xFF0000);
    CHECK(bank->size == 0x10000);
    CHECK(bank->guest_address_of(mem + 0x809) == 0xFF0809);
}

TEST_CASE("tagged_allocator gives each bank its own page boundary") {
    tagged_allocator alloc;
    auto* a = alloc.allocate_bank(memory_tag{chip_cpu, 0, 0}, 0x100);
    auto* b = alloc.allocate_bank(memory_tag{chip_cpu, 1, 0}, 0x100);

    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(a) % 4096U == 0U);
    CHECK(reinterpret_cast<std::uintptr_t>(b) % 4096U == 0U);
    CHECK(a != b);
}

TEST_CASE("tagged_allocator forgets a freed bank") {
    tagged_allocator alloc;
    auto* mem = alloc.allocate_bank(memory_tag{chip_cpu, 0, 0}, 0x1000);
    REQUIRE(alloc.registry().find_by_address(mem) != nullptr);

    alloc.free_bank(mem);
    CHECK(alloc.registry().find_by_address(mem) == nullptr);
}
