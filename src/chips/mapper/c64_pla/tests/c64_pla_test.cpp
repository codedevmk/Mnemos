#include "c64_pla.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::mapper::c64_pla;
    using region = c64_pla::region;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::imapper, c64_pla>);
static_assert(c64_pla::static_class == mnemos::chips::chip_class::mapper);

TEST_CASE("c64_pla reports identity and registers under mos.906114") {
    const c64_pla pla;
    const auto md = pla.metadata();
    CHECK(md.manufacturer == "MOS Technology");
    CHECK(md.part_number == "906114");
    CHECK(md.klass == mnemos::chips::chip_class::mapper);

    const auto* descriptor = mnemos::chips::find_factory("mos.906114");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::mapper);
    auto chip = mnemos::chips::create_chip("mos.906114");
    REQUIRE(chip != nullptr);
}

TEST_CASE("c64_pla default banking exposes BASIC, I/O, and KERNAL") {
    const c64_pla pla; // cold start: all inputs high, no cart
    CHECK(pla.decode_cpu_address(0x0000U) == region::ram);
    CHECK(pla.decode_cpu_address(0x8000U) == region::ram);
    CHECK(pla.decode_cpu_address(0xA000U) == region::basic);
    CHECK(pla.decode_cpu_address(0xC000U) == region::ram);
    CHECK(pla.decode_cpu_address(0xD000U) == region::io);
    CHECK(pla.decode_cpu_address(0xE000U) == region::kernal);
}

TEST_CASE("c64_pla CHAREN low maps the character ROM at $D000") {
    c64_pla pla;
    pla.set_cpu_port(true, true, false); // LORAM, HIRAM high; CHAREN low
    CHECK(pla.decode_cpu_address(0xD000U) == region::chargen);
}

TEST_CASE("c64_pla all-RAM when the port bits are low") {
    c64_pla pla;
    pla.set_cpu_port(false, false, true);
    CHECK(pla.decode_cpu_address(0xA000U) == region::ram); // BASIC gone
    CHECK(pla.decode_cpu_address(0xD000U) == region::ram); // I/O gone
    CHECK(pla.decode_cpu_address(0xE000U) == region::ram); // KERNAL gone
}

TEST_CASE("c64_pla 8 KB cart exposes ROML at $8000") {
    c64_pla pla;
    pla.set_cart_lines(true, false); // /GAME high, /EXROM low
    CHECK(pla.decode_cpu_address(0x8000U) == region::roml);
    CHECK(pla.decode_cpu_address(0xA000U) == region::basic); // unchanged
}

TEST_CASE("c64_pla 16 KB cart exposes ROML and ROMH") {
    c64_pla pla;
    pla.set_cart_lines(false, false); // /GAME low, /EXROM low
    CHECK(pla.decode_cpu_address(0x8000U) == region::roml);
    CHECK(pla.decode_cpu_address(0xA000U) == region::romh);
}

TEST_CASE("c64_pla ultimax mode hands the cart most of the map") {
    c64_pla pla;
    pla.set_cart_lines(false, true); // /GAME low, /EXROM high -> ultimax
    REQUIRE(pla.ultimax());
    CHECK(pla.decode_cpu_address(0x0000U) == region::ram);
    CHECK(pla.decode_cpu_address(0x1000U) == region::open);
    CHECK(pla.decode_cpu_address(0x8000U) == region::roml);
    CHECK(pla.decode_cpu_address(0xA000U) == region::open);
    CHECK(pla.decode_cpu_address(0xC000U) == region::open);
    CHECK(pla.decode_cpu_address(0xD000U) == region::io);
    CHECK(pla.decode_cpu_address(0xE000U) == region::romh);
}

TEST_CASE("c64_pla VIC fetch path sees CHARGEN and RAM, ignoring CHAREN") {
    c64_pla pla;
    pla.set_cpu_port(true, true, true); // CHAREN high does not affect VIC
    CHECK(pla.decode_vic_address(0x0000U) == region::ram);
    CHECK(pla.decode_vic_address(0x1000U) == region::chargen);
    CHECK(pla.decode_vic_address(0x2000U) == region::ram);
    CHECK(pla.decode_vic_address(0x9000U) == region::chargen); // bank-2 mirror

    pla.set_cart_lines(false, true); // ultimax: ROMH intercepts $3000-$3FFF
    CHECK(pla.decode_vic_address(0x3000U) == region::romh);
}

TEST_CASE("c64_pla reset restores the cold-start configuration") {
    c64_pla pla;
    pla.set_cpu_port(false, false, false);
    pla.set_cart_lines(false, false);

    pla.reset(mnemos::chips::reset_kind::power_on);
    CHECK(pla.loram());
    CHECK(pla.hiram());
    CHECK(pla.charen());
    CHECK(pla.game());
    CHECK(pla.exrom());
    CHECK(pla.decode_cpu_address(0xE000U) == region::kernal);
}

TEST_CASE("c64_pla register snapshot packs the five input bits") {
    c64_pla pla;
    pla.set_cpu_port(true, false, true); // LORAM=1, HIRAM=0, CHAREN=1
    pla.set_cart_lines(false, true);     // GAME=0, EXROM=1
    const auto regs = pla.register_snapshot();
    REQUIRE(regs.size() == 1U);
    CHECK(regs[0].name == "CONFIG");
    // bit0 LORAM=1, bit2 CHAREN=1, bit4 EXROM=1 -> 0b10101 = 0x15
    CHECK(regs[0].value == 0x15U);
}

TEST_CASE("c64_pla save/load round-trips") {
    c64_pla a;
    a.set_cpu_port(false, true, false);
    a.set_cart_lines(false, true);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    c64_pla b;
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());
    CHECK_FALSE(b.loram());
    CHECK(b.hiram());
    CHECK_FALSE(b.charen());
    CHECK_FALSE(b.game());
    CHECK(b.exrom());

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
