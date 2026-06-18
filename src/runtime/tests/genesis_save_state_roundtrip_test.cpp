// End-to-end whole-machine save/load/rewind determinism on a Genesis built through
// the manifest path (build_genesis_runtime -- the path the player runs).
//
// No copyrighted ROM is required: a tiny synthetic cart (valid reset vectors into a
// self-loop) boots deterministically through both buses, which is all these
// invariants need:
//
//   * the serialised save-state byte stream is identical across independent runs
//     (ARCH-004 §16: determinism of the save-state byte stream);
//   * loading a save reproduces the machine's forward evolution exactly
//     (save/load transparency -- the property rewind and netplay are built on);
//   * a rewind-ring seek + replay lands on the identical state;
//   * corrupt / mismatched / truncated states are rejected, and unknown chunks are
//     skipped (forward compatibility);
//   * a cart that declares battery SRAM round-trips its backing bytes + the mapper
//     control latches.
//
// The captured state is the whole machine: the five graph chips (68000 / Z80 / VDP
// / YM2612 / SN76489), the 64K work RAM + 8K Z80 RAM, the cartridge SRAM bytes, the
// non-chip board latches (genesis_runtime::save_state), and the scheduler's pacing.

#include "genesis_runtime.hpp"
#include "machine_save.hpp"
#include "rewind.hpp"
#include "save_state.hpp"
#include "scheduler.hpp"

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using namespace mnemos;

    constexpr std::string_view k_manifest_id = "sega.genesis.ntsc";
    constexpr std::uint32_t k_manifest_rev = 1U;

    // Minimal cart: SSP=$00FFFE00, PC=$00000008, then BRA.S * (self loop). Boots
    // cleanly through reset-vector load, ROM fetch, VDP raster, and the V-blank ->
    // Z80 edge -- same image the manifest-parity test drives.
    std::vector<std::uint8_t> loop_rom(std::size_t size = 0x1000U) {
        std::vector<std::uint8_t> rom(size, 0x00U);
        rom[0] = 0x00; // initial SSP (big-endian) = $00FFFE00
        rom[1] = 0xFF;
        rom[2] = 0xFE;
        rom[3] = 0x00;
        rom[4] = 0x00; // initial PC (big-endian) = $00000008
        rom[5] = 0x00;
        rom[6] = 0x00;
        rom[7] = 0x08;
        rom[8] = 0x60; // BRA.S *  (0x60 0xFE -> branch to self)
        rom[9] = 0xFE;
        return rom;
    }

    // loop_rom plus an external-RAM ("RA") header declaring a 1 KiB battery SRAM at
    // $200000-$2003FF (above the ROM image, so it powers on mapped + enabled).
    std::vector<std::uint8_t> sram_rom() {
        std::vector<std::uint8_t> rom = loop_rom();
        rom[0x1B0U] = static_cast<std::uint8_t>('R');
        rom[0x1B1U] = static_cast<std::uint8_t>('A');
        const std::array<std::uint8_t, 4> start{0x00U, 0x20U, 0x00U, 0x00U}; // $200000
        const std::array<std::uint8_t, 4> end{0x00U, 0x20U, 0x03U, 0xFFU};   // $2003FF
        for (std::size_t i = 0; i < 4U; ++i) {
            rom[0x1B4U + i] = start[i];
            rom[0x1B8U + i] = end[i];
        }
        return rom;
    }

    std::unique_ptr<manifests::genesis::genesis_runtime>
    make_genesis(const std::vector<std::uint8_t>& rom) {
        return manifests::genesis::build_genesis_runtime(rom);
    }

    runtime::scheduler make_scheduler(manifests::genesis::genesis_runtime& rt) {
        std::vector<runtime::scheduled_chip> slots;
        for (const manifests::genesis::scheduled_entry& entry : rt.schedule()) {
            slots.push_back(
                runtime::scheduled_chip{.chip = entry.chip,
                                        .divider = entry.weight == 0U ? 1U : entry.weight,
                                        .rate_num = 0U,
                                        .rate_den = 0U});
        }
        return runtime::scheduler(std::move(slots), rt.vdp());
    }

    // A deterministic, distinctive starting state so a save has real content to
    // preserve (rather than all-zero RAM that round-trips trivially). Seeds the
    // work + Z80 RAM and pokes a few board latches the system chunk carries.
    void seed(manifests::genesis::genesis_runtime& rt) {
        const std::span<std::uint8_t> work = rt.graph.region_span("work_ram");
        for (std::size_t i = 0; i < work.size(); ++i) {
            work[i] = static_cast<std::uint8_t>((i * 31U + 7U) & 0xFFU);
        }
        for (std::size_t i = 0; i < rt.state.z80_ram.size(); ++i) {
            rt.state.z80_ram[i] = static_cast<std::uint8_t>((i * 13U + 1U) & 0xFFU);
        }
        rt.state.z80_bank = 0x0123U;
        rt.state.io_regs[0x03U] = 0x5AU;
        if (!rt.sram.data.empty()) {
            for (std::size_t i = 0; i < rt.sram.data.size(); ++i) {
                rt.sram.data[i] = static_cast<std::uint8_t>((i * 7U + 3U) & 0xFFU);
            }
            rt.sram.write_protect = true;
        }
    }

    // The whole-machine save target: the generic builder supplies the manifest chips
    // (cpu/z80/video/fm/psg) and the work RAM region; the Z80 RAM + cartridge SRAM
    // are not graph regions, and the board latches and scheduler pacing are not chips
    // at all, so they are added here. Mirrors the C64 roundtrip target.
    runtime::save_target full_target(manifests::genesis::genesis_runtime& rt,
                                     runtime::scheduler& sched) {
        const std::array<std::string_view, 1> regions{"work_ram"};
        runtime::save_target target = runtime::build_save_target(
            rt.graph, std::string(k_manifest_id), k_manifest_rev, sched.master_cycle(), regions);

        target.memory.push_back(runtime::save_memory{
            .id = "z80_ram", .bytes = std::span<std::uint8_t>(rt.state.z80_ram)});
        if (!rt.sram.data.empty()) {
            target.memory.push_back(
                runtime::save_memory{.id = "sram", .bytes = std::span<std::uint8_t>(rt.sram.data)});
        }

        target.components.push_back(runtime::save_component{
            .id = "system",
            .save = [&rt](chips::state_writer& writer) { rt.save_state(writer); },
            .load = [&rt](chips::state_reader& reader) { rt.load_state(reader); }});
        if (rt.eeprom.device.has_value()) {
            target.components.push_back(runtime::save_component{
                .id = "eeprom",
                .save =
                    [&rt](chips::state_writer& writer) { rt.eeprom.device->save_state(writer); },
                .load =
                    [&rt](chips::state_reader& reader) { rt.eeprom.device->load_state(reader); }});
        }
        target.components.push_back(runtime::save_component{
            .id = "scheduler",
            .save = [&sched](chips::state_writer& writer) { sched.save_state(writer); },
            .load = [&sched](chips::state_reader& reader) { sched.load_state(reader); }});
        return target;
    }

    std::vector<std::uint8_t> snapshot(manifests::genesis::genesis_runtime& rt,
                                       runtime::scheduler& sched) {
        runtime::save_target target = full_target(rt, sched);
        target.master_cycle = sched.master_cycle();
        return runtime::write_save_state(target);
    }

} // namespace

TEST_CASE("Genesis machine exposes the writable regions the save target needs") {
    const std::unique_ptr<manifests::genesis::genesis_runtime> rt = make_genesis(loop_rom());
    REQUIRE(rt != nullptr);
    CHECK(rt->graph.region_span("work_ram").size() == 0x10000U);
    CHECK(rt->state.z80_ram.size() == 0x2000U);
}

TEST_CASE("Genesis save-state byte stream is deterministic across independent runs") {
    const std::unique_ptr<manifests::genesis::genesis_runtime> a = make_genesis(loop_rom());
    const std::unique_ptr<manifests::genesis::genesis_runtime> b = make_genesis(loop_rom());
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    runtime::scheduler sa = make_scheduler(*a);
    runtime::scheduler sb = make_scheduler(*b);
    seed(*a);
    seed(*b);
    sa.run_frames(8U);
    sb.run_frames(8U);

    const std::vector<std::uint8_t> da = snapshot(*a, sa);
    const std::vector<std::uint8_t> db = snapshot(*b, sb);

    REQUIRE(!da.empty());
    CHECK(sa.master_cycle() == sb.master_cycle());
    CHECK(da == db);
}

TEST_CASE("Genesis save/load reproduces forward evolution exactly") {
    const std::unique_ptr<manifests::genesis::genesis_runtime> a = make_genesis(loop_rom());
    REQUIRE(a != nullptr);
    runtime::scheduler sa = make_scheduler(*a);
    seed(*a);

    sa.run_frames(8U);
    const std::uint64_t cycle_at_save = sa.master_cycle();
    const std::vector<std::uint8_t> state_at_8 = snapshot(*a, sa);

    // Reference trajectory: continue eight more frames from the live machine.
    sa.run_frames(8U);
    const std::vector<std::uint8_t> reference_at_16 = snapshot(*a, sa);
    CHECK(sa.master_cycle() != cycle_at_save);

    // Restore the frame-8 snapshot into the same machine and replay eight frames.
    runtime::save_target target = full_target(*a, sa);
    const runtime::load_result loaded = runtime::read_save_state(state_at_8, target);
    REQUIRE(loaded.ok());
    CHECK(loaded.master_cycle == cycle_at_save);
    CHECK(sa.master_cycle() == cycle_at_save); // scheduler pacing was restored
    sa.run_frames(8U);

    const std::vector<std::uint8_t> replayed_at_16 = snapshot(*a, sa);
    CHECK(replayed_at_16 == reference_at_16);
}

TEST_CASE("Genesis rewind-ring seek and replay is deterministic") {
    const std::unique_ptr<manifests::genesis::genesis_runtime> a = make_genesis(loop_rom());
    REQUIRE(a != nullptr);
    runtime::scheduler sa = make_scheduler(*a);
    seed(*a);

    runtime::rewind_ring ring(32U);
    std::vector<std::uint64_t> cycle_at_frame(1U, 0U); // 1-based index by frame
    for (std::uint64_t frame = 1U; frame <= 12U; ++frame) {
        sa.run_frame();
        ring.push(frame, snapshot(*a, sa));
        cycle_at_frame.push_back(sa.master_cycle());
    }
    const std::vector<std::uint8_t> final_at_12 = snapshot(*a, sa);

    const std::vector<std::uint8_t>* const seek = ring.at_or_before(4U);
    REQUIRE(seek != nullptr);

    runtime::save_target target = full_target(*a, sa);
    REQUIRE(runtime::read_save_state(*seek, target).ok());
    REQUIRE(sa.master_cycle() == cycle_at_frame[4]);

    sa.run_frames(12U - 4U);
    const std::vector<std::uint8_t> replayed_at_12 = snapshot(*a, sa);
    CHECK(sa.master_cycle() == cycle_at_frame[12]);
    CHECK(replayed_at_12 == final_at_12);
}

TEST_CASE("Genesis save-state load rejects corruption and mismatch") {
    const std::unique_ptr<manifests::genesis::genesis_runtime> a = make_genesis(loop_rom());
    REQUIRE(a != nullptr);
    runtime::scheduler sa = make_scheduler(*a);
    seed(*a);
    sa.run_frames(4U);
    const std::vector<std::uint8_t> good = snapshot(*a, sa);
    REQUIRE(good.size() > 16U);

    runtime::save_target target = full_target(*a, sa);

    SECTION("bad magic") {
        std::vector<std::uint8_t> bad = good;
        bad[0] = static_cast<std::uint8_t>(bad[0] ^ 0xFFU);
        CHECK(runtime::read_save_state(bad, target).status == runtime::load_status::bad_magic);
    }
    SECTION("corrupted body fails the CRC") {
        std::vector<std::uint8_t> bad = good;
        bad[bad.size() - 6U] = static_cast<std::uint8_t>(bad[bad.size() - 6U] ^ 0xFFU);
        CHECK(runtime::read_save_state(bad, target).status == runtime::load_status::bad_crc);
    }
    SECTION("manifest id mismatch") {
        runtime::save_target other = full_target(*a, sa);
        other.manifest_id = "sega.genesis.pal";
        CHECK(runtime::read_save_state(good, other).status ==
              runtime::load_status::manifest_mismatch);
    }
    SECTION("a partial target skips unknown chunks and still loads") {
        const std::array<std::string_view, 1> regions{"work_ram"};
        runtime::save_target partial = runtime::build_save_target(
            a->graph, std::string(k_manifest_id), k_manifest_rev, sa.master_cycle(), regions);
        CHECK(runtime::read_save_state(good, partial).ok());
    }
    SECTION("a valid load still succeeds") { CHECK(runtime::read_save_state(good, target).ok()); }
}

TEST_CASE("Genesis battery-SRAM cart round-trips its backing bytes and mapper latches") {
    const std::unique_ptr<manifests::genesis::genesis_runtime> a = make_genesis(sram_rom());
    REQUIRE(a != nullptr);
    REQUIRE(a->sram.data.size() == 0x400U); // 1 KiB declared by the "RA" header

    runtime::scheduler sa = make_scheduler(*a);
    seed(*a); // fills sram.data + sets write_protect
    sa.run_frames(4U);
    const std::vector<std::uint8_t> saved = snapshot(*a, sa);
    const std::vector<std::uint8_t> sram_at_save = a->sram.data;
    const bool wp_at_save = a->sram.write_protect;

    // Clobber the SRAM bytes + the control latch, then restore and confirm both came
    // back from the snapshot (the bytes via the "sram" memory chunk, the WP flag via
    // the system latch chunk).
    for (std::uint8_t& b : a->sram.data) {
        b = static_cast<std::uint8_t>(b ^ 0xFFU);
    }
    a->sram.write_protect = !wp_at_save;

    runtime::save_target target = full_target(*a, sa);
    REQUIRE(runtime::read_save_state(saved, target).ok());
    CHECK(a->sram.data == sram_at_save);
    CHECK(a->sram.write_protect == wp_at_save);
}
