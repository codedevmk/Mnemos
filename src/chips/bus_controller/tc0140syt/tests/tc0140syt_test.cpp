#include "tc0140syt.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::bus_controller::tc0140syt;

    [[nodiscard]] std::uint64_t reg_value(std::span<const mnemos::chips::register_descriptor> regs,
                                          std::string_view name) {
        for (const auto& reg : regs) {
            if (reg.name == name) {
                return reg.value;
            }
        }
        FAIL("missing TC0140SYT register " << name);
        return 0U;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ibus_controller, tc0140syt>);

TEST_CASE("tc0140syt reports identity and factory registration") {
    const tc0140syt chip;
    const auto md = chip.metadata();
    CHECK(md.manufacturer == "Taito");
    CHECK(md.part_number == "TC0140SYT");
    CHECK(md.klass == mnemos::chips::chip_class::bus_controller);

    REQUIRE(mnemos::chips::find_factory("taito.tc0140syt") != nullptr);
    REQUIRE(mnemos::chips::create_chip("taito.tc0140syt") != nullptr);
}

TEST_CASE("tc0140syt reset exposes empty pending state") {
    tc0140syt chip;
    CHECK(chip.command_latch(0U) == 0xFFU);
    CHECK(chip.reply_latch(1U) == 0xFFU);
    CHECK(chip.status() == 0U);
    CHECK(chip.command_nmi_pulses() == 0U);

    auto* regs = chip.introspection().registers();
    REQUIRE(regs != nullptr);
    const auto snapshot = regs->registers();
    CHECK(reg_value(snapshot, "M2S0") == 0xFFU);
    CHECK(reg_value(snapshot, "STATUS") == 0U);
}

TEST_CASE("tc0140syt register snapshot tracks ports, latches, pending bits, and NMI pulses") {
    tc0140syt chip;
    chip.main_port() = 4U;
    chip.sound_port() = 2U;
    chip.command_latch(0U) = 0x5AU;
    chip.reply_latch(1U) = 0xC3U;
    chip.command_pending(0U) = true;
    chip.reply_pending(1U) = true;
    chip.main_write_high() = true;
    chip.sound_read_high() = true;
    chip.note_command_nmi_pulse();
    chip.note_command_write(0U, 0x5AU);
    chip.note_command_read(0U, 0x5AU);
    chip.note_reply_write(1U, 0xC3U);
    chip.note_reply_read(1U, 0xC3U);
    chip.clear_all_pending();

    const auto snapshot = chip.register_snapshot();
    CHECK(reg_value(snapshot, "MPORT") == 4U);
    CHECK(reg_value(snapshot, "SPORT") == 2U);
    CHECK(reg_value(snapshot, "M2S0") == 0x5AU);
    CHECK(reg_value(snapshot, "S2M1") == 0xC3U);
    CHECK(reg_value(snapshot, "STATUS") == 0x00U);
    CHECK(reg_value(snapshot, "M2SPEND") == 0x00U);
    CHECK(reg_value(snapshot, "S2MPEND") == 0x00U);
    CHECK(reg_value(snapshot, "MWRPH") == 1U);
    CHECK(reg_value(snapshot, "SRDPH") == 1U);
    CHECK(reg_value(snapshot, "CMDNMI") == 1U);
    CHECK(reg_value(snapshot, "CMDW0") == 1U);
    CHECK(reg_value(snapshot, "CMDR0") == 1U);
    CHECK(reg_value(snapshot, "RPLW1") == 1U);
    CHECK(reg_value(snapshot, "RPLR1") == 1U);
    CHECK(reg_value(snapshot, "CLEAR") == 1U);
    CHECK(reg_value(snapshot, "LCMDP") == 0U);
    CHECK(reg_value(snapshot, "LCMD") == 0x5AU);
    CHECK(reg_value(snapshot, "LCMR") == 0x5AU);
    CHECK(reg_value(snapshot, "LRPW") == 0xC3U);
    CHECK(reg_value(snapshot, "LRPR") == 0xC3U);
}

TEST_CASE("tc0140syt save/load round-trips diagnostic state") {
    tc0140syt chip;
    chip.command_latch(1U) = 0xA5U;
    chip.reply_latch(0U) = 0x3CU;
    chip.command_pending(1U) = true;
    chip.reply_pending(0U) = true;
    chip.main_port() = 2U;
    chip.sound_port() = 4U;
    chip.main_read_high() = true;
    chip.sound_write_high() = true;
    chip.set_command_nmi_pulses(7U);
    chip.note_command_write(1U, 0xA5U);
    chip.note_command_read(1U, 0xA5U);
    chip.note_reply_write(0U, 0x3CU);
    chip.note_reply_read(0U, 0x3CU);
    chip.clear_all_pending();

    std::vector<std::uint8_t> bytes;
    {
        mnemos::chips::state_writer writer(bytes);
        chip.save_state(writer);
    }

    tc0140syt restored;
    {
        mnemos::chips::state_reader reader(bytes);
        restored.load_state(reader);
        REQUIRE(reader.ok());
    }

    CHECK(restored.command_latch(1U) == 0xA5U);
    CHECK(restored.reply_latch(0U) == 0x3CU);
    CHECK(restored.status() == 0x00U);
    CHECK(restored.main_port() == 2U);
    CHECK(restored.sound_port() == 4U);
    CHECK(restored.main_read_high());
    CHECK(restored.sound_write_high());
    CHECK(restored.command_nmi_pulses() == 7U);
    CHECK(restored.command_write_count(1U) == 1U);
    CHECK(restored.command_read_count(1U) == 1U);
    CHECK(restored.reply_write_count(0U) == 1U);
    CHECK(restored.reply_read_count(0U) == 1U);
    CHECK(restored.clear_count() == 1U);
}
