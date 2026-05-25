#include <mnemos/chips/peripheral/reu.hpp>

#include <mnemos/chips/common/bus.hpp>
#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::peripheral::reu;

    class flat_ram final : public mnemos::chips::i_bus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};
        [[nodiscard]] std::uint8_t read8(std::uint32_t a) override { return memory[a & 0xFFFFU]; }
        void write8(std::uint32_t a, std::uint8_t v) override { memory[a & 0xFFFFU] = v; }
    };

    // Program the transfer registers and run with the given command (+execute).
    void run(reu& unit, std::uint16_t c64, std::uint32_t reu_addr, std::uint16_t length,
             std::uint8_t command) {
        unit.mmio_write(0x02U, static_cast<std::uint8_t>(c64 & 0xFFU));
        unit.mmio_write(0x03U, static_cast<std::uint8_t>(c64 >> 8U));
        unit.mmio_write(0x04U, static_cast<std::uint8_t>(reu_addr & 0xFFU));
        unit.mmio_write(0x05U, static_cast<std::uint8_t>((reu_addr >> 8U) & 0xFFU));
        unit.mmio_write(0x06U, static_cast<std::uint8_t>((reu_addr >> 16U) & 0xFFU));
        unit.mmio_write(0x07U, static_cast<std::uint8_t>(length & 0xFFU));
        unit.mmio_write(0x08U, static_cast<std::uint8_t>(length >> 8U));
        unit.mmio_write(0x01U, static_cast<std::uint8_t>(command | 0x80U));
    }

} // namespace

TEST_CASE("reu registers and sizes its RAM") {
    REQUIRE(mnemos::chips::find_factory("commodore.1750") != nullptr);
    REQUIRE(mnemos::chips::create_chip("commodore.1750") != nullptr);
    CHECK(reu(reu::model::ram_128k).ram_size() == 128U * 1024U);
    CHECK(reu(reu::model::ram_512k).ram_size() == 512U * 1024U);
}

TEST_CASE("reu stashes C64 memory into expansion RAM") {
    flat_ram bus;
    reu unit(reu::model::ram_128k);
    unit.attach_bus(bus);
    for (std::uint16_t i = 0; i < 4U; ++i) {
        bus.memory[0x1000U + i] = static_cast<std::uint8_t>(0xA0U + i);
    }
    run(unit, 0x1000U, 0U, 4U, 0x00U); // stash
    for (std::size_t i = 0; i < 4U; ++i) {
        CHECK(unit.peek(i) == static_cast<std::uint8_t>(0xA0U + i));
    }
    CHECK((unit.mmio_read(0x00U) & 0x40U) != 0U); // end-of-block latched
}

TEST_CASE("reu fetches expansion RAM into C64 memory") {
    flat_ram bus;
    reu unit(reu::model::ram_128k);
    unit.attach_bus(bus);
    for (std::size_t i = 0; i < 4U; ++i) {
        unit.poke(i, static_cast<std::uint8_t>(0x50U + i));
    }
    run(unit, 0x2000U, 0U, 4U, 0x01U); // fetch
    for (std::uint16_t i = 0; i < 4U; ++i) {
        CHECK(bus.memory[0x2000U + i] == static_cast<std::uint8_t>(0x50U + i));
    }
}

TEST_CASE("reu swap exchanges C64 and expansion bytes") {
    flat_ram bus;
    reu unit(reu::model::ram_128k);
    unit.attach_bus(bus);
    bus.memory[0x3000U] = 0xC1U;
    unit.poke(0U, 0xD2U);
    run(unit, 0x3000U, 0U, 1U, 0x02U); // swap
    CHECK(bus.memory[0x3000U] == 0xD2U);
    CHECK(unit.peek(0U) == 0xC1U);
}

TEST_CASE("reu verify flags a fault on mismatch") {
    flat_ram bus;
    reu unit(reu::model::ram_128k);
    unit.attach_bus(bus);
    bus.memory[0x4000U] = 0xEEU;
    unit.poke(0U, 0xEEU);
    run(unit, 0x4000U, 0U, 1U, 0x03U); // verify, match
    CHECK((unit.mmio_read(0x00U) & 0x20U) == 0U); // no fault

    unit.poke(0U, 0x01U);              // now differs
    run(unit, 0x4000U, 0U, 1U, 0x03U); // verify, mismatch
    CHECK((unit.mmio_read(0x00U) & 0x20U) != 0U); // fault latched
}

TEST_CASE("reu honours the fixed-address control bits") {
    flat_ram bus;
    reu unit(reu::model::ram_128k);
    unit.attach_bus(bus);
    bus.memory[0x5000U] = 0x7FU;
    unit.mmio_write(0x0AU, 0x80U);     // fix the C64 address
    run(unit, 0x5000U, 0U, 4U, 0x00U); // stash 4 bytes from the same C64 byte
    for (std::size_t i = 0; i < 4U; ++i) {
        CHECK(unit.peek(i) == 0x7FU); // all four came from $5000
    }
}

TEST_CASE("reu save/load round-trips") {
    flat_ram bus;
    reu a(reu::model::ram_128k);
    a.attach_bus(bus);
    bus.memory[0x1000U] = 0x99U;
    run(a, 0x1000U, 0U, 1U, 0x00U);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    reu b(reu::model::ram_128k);
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());
    CHECK(b.peek(0U) == 0x99U);

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
