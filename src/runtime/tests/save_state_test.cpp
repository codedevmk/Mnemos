#include "save_state.hpp"

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using namespace mnemos;

    struct introspection_stub final : instrumentation::ichip_introspection {};

    // A chip whose entire state is one 32-bit value.
    struct stateful_chip final : chips::ichip {
        std::uint32_t value{};

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override { return {}; }
        void tick(std::uint64_t) override {}
        void reset(chips::reset_kind) override { value = 0U; }
        void save_state(chips::state_writer& w) const override { w.u32(value); }
        void load_state(chips::state_reader& r) override { value = r.u32(); }
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return intro_;
        }

      private:
        introspection_stub intro_;
    };

    struct rejecting_chip final : chips::ichip {
        [[nodiscard]] chips::chip_metadata metadata() const noexcept override { return {}; }
        void tick(std::uint64_t) override {}
        void reset(chips::reset_kind) override {}
        void save_state(chips::state_writer& w) const override { w.u32(0xBADC0DEU); }
        void load_state(chips::state_reader& r) override {
            static_cast<void>(r.u32());
            r.fail();
        }
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return intro_;
        }

      private:
        introspection_stub intro_;
    };

} // namespace

TEST_CASE("save state round-trips chips, memory, and the master cycle") {
    stateful_chip cpu;
    cpu.value = 0xDEADBEEFU;
    std::vector<std::uint8_t> ram = {1U, 2U, 3U, 4U, 5U};

    runtime::save_target src;
    src.manifest_id = "commodore.c64.pal";
    src.manifest_rev = 1U;
    src.master_cycle = 123456U;
    src.chips.push_back({.id = "cpu", .chip = &cpu});
    src.memory.push_back({.id = "ram", .bytes = std::span<std::uint8_t>(ram)});

    const std::vector<std::uint8_t> blob = runtime::write_save_state(src);
    REQUIRE(blob.size() > 8U);

    stateful_chip restored_cpu; // value 0
    std::vector<std::uint8_t> restored_ram(ram.size(), 0U);
    runtime::save_target dst;
    dst.manifest_id = "commodore.c64.pal";
    dst.manifest_rev = 1U;
    dst.chips.push_back({.id = "cpu", .chip = &restored_cpu});
    dst.memory.push_back({.id = "ram", .bytes = std::span<std::uint8_t>(restored_ram)});

    const runtime::load_result result = runtime::read_save_state(blob, dst);
    REQUIRE(result.ok());
    CHECK(result.master_cycle == 123456U);
    CHECK(restored_cpu.value == 0xDEADBEEFU);
    CHECK(restored_ram == ram);
}

TEST_CASE("save state rejects corruption and mismatches") {
    stateful_chip chip;
    chip.value = 0x12345678U;
    runtime::save_target src;
    src.manifest_id = "sys";
    src.chips.push_back({.id = "c", .chip = &chip});
    const std::vector<std::uint8_t> blob = runtime::write_save_state(src);

    SECTION("bad magic") {
        std::vector<std::uint8_t> bad = blob;
        bad[0] ^= 0xFFU;
        stateful_chip c2;
        runtime::save_target dst;
        dst.manifest_id = "sys";
        dst.chips.push_back({.id = "c", .chip = &c2});
        CHECK(runtime::read_save_state(bad, dst).status == runtime::load_status::bad_magic);
    }
    SECTION("CRC mismatch on a flipped payload byte") {
        std::vector<std::uint8_t> bad = blob;
        bad[bad.size() - 6U] ^= 0x01U; // inside the body, before the CRC
        stateful_chip c2;
        runtime::save_target dst;
        dst.manifest_id = "sys";
        dst.chips.push_back({.id = "c", .chip = &c2});
        CHECK(runtime::read_save_state(bad, dst).status == runtime::load_status::bad_crc);
    }
    SECTION("manifest id mismatch") {
        stateful_chip c2;
        runtime::save_target dst;
        dst.manifest_id = "other";
        dst.chips.push_back({.id = "c", .chip = &c2});
        CHECK(runtime::read_save_state(blob, dst).status ==
              runtime::load_status::manifest_mismatch);
    }
    SECTION("manifest revision mismatch") {
        stateful_chip c2;
        runtime::save_target dst;
        dst.manifest_id = "sys";
        dst.manifest_rev = 1U;
        dst.chips.push_back({.id = "c", .chip = &c2});
        CHECK(runtime::read_save_state(blob, dst).status ==
              runtime::load_status::manifest_mismatch);
    }
}

TEST_CASE("save state skips chunks with no matching target") {
    stateful_chip chip;
    chip.value = 0x99U;
    runtime::save_target src;
    src.manifest_id = "sys";
    src.chips.push_back({.id = "absent", .chip = &chip});
    const std::vector<std::uint8_t> blob = runtime::write_save_state(src);

    stateful_chip other;
    other.value = 0x55U;
    runtime::save_target dst;
    dst.manifest_id = "sys";
    dst.chips.push_back({.id = "different", .chip = &other});

    const runtime::load_result result = runtime::read_save_state(blob, dst);
    CHECK(result.ok());
    CHECK(other.value == 0x55U); // untouched: the "absent" chunk had no home
}

TEST_CASE("save state reports rejected chip and component chunks") {
    stateful_chip chip;
    chip.value = 0x1234U;
    runtime::save_target src;
    src.manifest_id = "sys";
    src.chips.push_back({.id = "chip", .chip = &chip});
    src.components.push_back({.id = "component",
                              .save = [](chips::state_writer& w) { w.u32(0x5678U); },
                              .load = [](chips::state_reader&) {}});
    const std::vector<std::uint8_t> blob = runtime::write_save_state(src);

    SECTION("chip reader failure") {
        rejecting_chip bad;
        runtime::save_target dst;
        dst.manifest_id = "sys";
        dst.chips.push_back({.id = "chip", .chip = &bad});
        CHECK(runtime::read_save_state(blob, dst).status == runtime::load_status::chunk_rejected);
    }

    SECTION("component reader failure") {
        stateful_chip ok;
        runtime::save_target dst;
        dst.manifest_id = "sys";
        dst.chips.push_back({.id = "chip", .chip = &ok});
        dst.components.push_back({.id = "component",
                                  .save = [](chips::state_writer&) {},
                                  .load = [](chips::state_reader& r) { r.fail(); }});
        CHECK(runtime::read_save_state(blob, dst).status == runtime::load_status::chunk_rejected);
    }
}
