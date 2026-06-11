#include "pic_8259.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::bus_controller::pic_8259;

    // The single-PIC init a typical board firmware performs: edge-triggered,
    // single (no ICW3), ICW4 follows; vector base 0x20, all lines unmasked.
    void init_single(pic_8259& pic, std::uint8_t base, std::uint8_t icw4 = 0x01U) {
        pic.write(0U, 0x13U); // ICW1: edge, SNGL=1, IC4=1
        pic.write(1U, base);  // ICW2
        pic.write(1U, icw4);  // ICW4: 8086 mode
        pic.write(1U, 0x00U); // OCW1: unmask all
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ibus_controller, pic_8259>);

TEST_CASE("pic_8259 reports identity and registers under intel.8259a") {
    const pic_8259 pic;
    const auto md = pic.metadata();
    CHECK(md.manufacturer == "Intel");
    CHECK(md.part_number == "8259A");
    CHECK(md.klass == mnemos::chips::chip_class::bus_controller);

    REQUIRE(mnemos::chips::find_factory("intel.8259a") != nullptr);
    REQUIRE(mnemos::chips::create_chip("intel.8259a") != nullptr);
}

TEST_CASE("pic_8259 SNGL init sequence routes ICW2 then ICW4, then OCW1 masks") {
    pic_8259 pic;
    pic.write(0U, 0x13U); // ICW1: SNGL=1, IC4=1
    pic.write(1U, 0x40U); // ICW2 -> base
    pic.write(1U, 0x01U); // ICW4
    CHECK(pic.vector_base() == 0x40U);
    CHECK(pic.imr() == 0U);

    pic.write(1U, 0xFAU); // now OCW1
    CHECK(pic.imr() == 0xFAU);
}

TEST_CASE("pic_8259 cascade init consumes an ICW3 between ICW2 and ICW4") {
    pic_8259 pic;
    pic.write(0U, 0x11U); // ICW1: SNGL=0, IC4=1
    pic.write(1U, 0x20U); // ICW2
    pic.write(1U, 0x04U); // ICW3 (slave map) -- must not be taken as ICW4/OCW1
    pic.write(1U, 0x01U); // ICW4
    CHECK(pic.vector_base() == 0x20U);
    CHECK(pic.imr() == 0U);
}

TEST_CASE("pic_8259 edge request asserts INT and acknowledge yields base+line") {
    pic_8259 pic;
    bool int_line = false;
    pic.set_int_callback([&](bool asserted) { int_line = asserted; });
    init_single(pic, 0x20U);

    pic.set_irq_line(2U, true);
    CHECK(int_line);
    CHECK(pic.acknowledge() == 0x22U);
    CHECK_FALSE(int_line); // in service, nothing else pending
    CHECK(pic.isr() == 0x04U);

    // Non-specific EOI releases the level.
    pic.write(0U, 0x20U);
    CHECK(pic.isr() == 0U);
}

TEST_CASE("pic_8259 fixed priority serves IR0 over IR2 and nests by rank") {
    pic_8259 pic;
    init_single(pic, 0x20U);

    pic.set_irq_line(2U, true);
    pic.set_irq_line(0U, true);
    CHECK(pic.acknowledge() == 0x20U); // IR0 wins
    // IR2 still pending and outranked by nothing in service below it? IR0 in
    // service outranks IR2: INT stays low until EOI.
    CHECK_FALSE(pic.int_asserted());
    pic.write(0U, 0x20U); // EOI IR0
    CHECK(pic.int_asserted());
    CHECK(pic.acknowledge() == 0x22U);
}

TEST_CASE("pic_8259 withdrawing a request before acknowledge forgets it") {
    pic_8259 pic;
    init_single(pic, 0x20U);

    pic.set_irq_line(0U, true);
    CHECK(pic.int_asserted());
    pic.set_irq_line(0U, false);
    CHECK_FALSE(pic.int_asserted());
    // A spurious acknowledge (CPU already committed) answers IR7.
    CHECK(pic.acknowledge() == 0x27U);
    CHECK(pic.isr() == 0U);
}

TEST_CASE("pic_8259 edge mode latches one request per rising edge") {
    pic_8259 pic;
    init_single(pic, 0x20U);

    pic.set_irq_line(1U, true);
    pic.set_irq_line(1U, true); // held high: no second edge
    CHECK(pic.acknowledge() == 0x21U);
    pic.write(0U, 0x20U); // EOI
    CHECK_FALSE(pic.int_asserted());

    pic.set_irq_line(1U, false);
    pic.set_irq_line(1U, true);
    CHECK(pic.int_asserted());
}

TEST_CASE("pic_8259 OCW1 masking gates INT without losing the request") {
    pic_8259 pic;
    init_single(pic, 0x20U);
    pic.write(1U, 0x01U); // mask IR0

    pic.set_irq_line(0U, true);
    CHECK_FALSE(pic.int_asserted());
    pic.write(1U, 0x00U); // unmask
    CHECK(pic.int_asserted());
    CHECK(pic.acknowledge() == 0x20U);
}

TEST_CASE("pic_8259 AEOI mode skips the in-service latch") {
    pic_8259 pic;
    init_single(pic, 0x20U, 0x03U); // ICW4 AEOI

    pic.set_irq_line(3U, true);
    CHECK(pic.acknowledge() == 0x23U);
    CHECK(pic.isr() == 0U); // no EOI needed
    // The same level can interrupt again on the next edge.
    pic.set_irq_line(3U, false);
    pic.set_irq_line(3U, true);
    CHECK(pic.int_asserted());
}

TEST_CASE("pic_8259 specific EOI clears the named level only") {
    pic_8259 pic;
    init_single(pic, 0x20U);

    pic.set_irq_line(0U, true);
    (void)pic.acknowledge();
    pic.set_irq_line(2U, true);
    pic.write(0U, 0x20U); // EOI IR0; IR2 now interrupts
    (void)pic.acknowledge();
    CHECK(pic.isr() == 0x04U);
    pic.write(0U, 0x62U); // specific EOI level 2
    CHECK(pic.isr() == 0U);
}

TEST_CASE("pic_8259 OCW3 selects ISR readback at A0=0") {
    pic_8259 pic;
    init_single(pic, 0x20U);

    pic.set_irq_line(5U, true);
    CHECK(pic.read(0U) == 0x20U); // IRR by default
    (void)pic.acknowledge();
    pic.write(0U, 0x0BU); // OCW3: RR=1, RIS=1 -> read ISR
    CHECK(pic.read(0U) == 0x20U);
    pic.write(0U, 0x0AU); // back to IRR
    CHECK(pic.read(0U) == 0x00U);
    CHECK(pic.read(1U) == pic.imr());
}

TEST_CASE("pic_8259 save/load round-trips the programmed state") {
    pic_8259 pic;
    init_single(pic, 0x40U);
    pic.write(1U, 0x10U); // OCW1
    pic.set_irq_line(2U, true);

    std::vector<std::uint8_t> bytes;
    {
        mnemos::chips::state_writer writer(bytes);
        pic.save_state(writer);
    }
    pic_8259 restored;
    {
        mnemos::chips::state_reader reader(bytes);
        restored.load_state(reader);
    }
    CHECK(restored.vector_base() == 0x40U);
    CHECK(restored.imr() == 0x10U);
    CHECK(restored.irr() == 0x04U);
}
