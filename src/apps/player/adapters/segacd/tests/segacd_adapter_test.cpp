// Phase D2: the segacd_adapter drives a full Sega CD frame -- the Genesis
// scheduler runs the main 68000 (which boots the BIOS), and the per-frame
// accumulator runs the sub-CPU + CD frames. A synthetic BIOS marks work RAM and
// releases the sub-CPU so we can see both halves advance.

#include "segacd_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::segacd::segacd_adapter;

    // 128 KB BIOS. Reset SSP=$00FF0000, PC=$000200. The main program writes a
    // $BEEF marker to work RAM, releases the sub-CPU (gate $A12001 bit0), and
    // spins.
    std::vector<std::uint8_t> make_bios() {
        std::vector<std::uint8_t> bios(0x20000, 0);
        bios[0] = 0x00;
        bios[1] = 0xFF;
        bios[2] = 0x00;
        bios[3] = 0x00; // SSP = $00FF0000
        bios[4] = 0x00;
        bios[5] = 0x00;
        bios[6] = 0x02;
        bios[7] = 0x00; // PC  = $000200

        std::size_t p = 0x200;
        const auto put = [&](std::initializer_list<std::uint8_t> bytes) {
            for (const std::uint8_t b : bytes) {
                bios[p++] = b;
            }
        };
        put({0x33, 0xFC, 0xBE, 0xEF, 0x00, 0xFF, 0x00, 0x00}); // MOVE.W #$BEEF,($00FF0000).L
        put({0x13, 0xFC, 0x00, 0x01, 0x00, 0xA1, 0x20, 0x01}); // MOVE.B #$01,($00A12001).L
        put({0x60, 0xFE});                                     // BRA *
        return bios;
    }

} // namespace

TEST_CASE("segacd_adapter boots the BIOS and runs both CPUs", "[segacd][adapter]") {
    segacd_adapter adapter(make_bios());

    for (int i = 0; i < 3; ++i) {
        adapter.step_one_frame();
    }
    REQUIRE(adapter.frames_stepped() == 3U);

    // The main 68000 executed the BIOS program (marker landed in work RAM).
    REQUIRE(adapter.machine().genesis->work_ram[0] == 0xBE);
    REQUIRE(adapter.machine().genesis->work_ram[1] == 0xEF);

    // The BIOS released the sub-CPU, which the adapter then ran.
    REQUIRE(adapter.machine().sub->sub_cpu.elapsed_cycles() > 0U);

    // The adapter presents a framebuffer + a sane refresh rate.
    REQUIRE(adapter.region().frames_per_second_x1000 > 0U);
}

TEST_CASE("segacd_adapter holds the sub-CPU until the BIOS releases it", "[segacd][adapter]") {
    // A BIOS that never touches gate $01: the sub-CPU must stay halted.
    std::vector<std::uint8_t> bios(0x20000, 0);
    bios[1] = 0xFF;     // SSP = $00FF0000
    bios[6] = 0x02;     // PC  = $000200
    bios[0x200] = 0x60; // BRA *
    bios[0x201] = 0xFE;

    segacd_adapter adapter(std::move(bios));
    adapter.step_one_frame();
    adapter.step_one_frame();
    REQUIRE(adapter.machine().sub->sub_cpu.elapsed_cycles() == 0U); // never released
}

TEST_CASE("segacd_adapter mounts a CD image from a path", "[segacd][adapter]") {
    namespace fs = std::filesystem;
    const fs::path iso = fs::temp_directory_path() / "mnemos_segacd_d3.iso";
    {
        const std::vector<std::uint8_t> data(4U * 2048U, 0xA5); // 4 user-data sectors
        std::ofstream os(iso, std::ios::binary);
        os.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    }

    segacd_adapter adapter(make_bios(), {}, "Test Disc", nullptr, iso.string());
    REQUIRE(adapter.machine().sub->disc != nullptr); // disc handed to the sub side
    REQUIRE(adapter.machine().sub->cdd_loaded);

    std::error_code ec;
    fs::remove(iso, ec);
}

TEST_CASE("segacd_adapter mixes CD-DA into the audio output", "[segacd][adapter][audio]") {
    // 2-sector image: sector 0 a valid Mode-1 sector (so open_bin accepts it),
    // sector 1 a loud constant PCM tone (L=R=0x4000).
    std::vector<std::uint8_t> bin(2U * 2352U, 0);
    bin[0] = 0x00;
    for (std::size_t i = 1; i <= 10; ++i) {
        bin[i] = 0xFF;
    }
    bin[11] = 0x00;
    bin[15] = 0x01;
    for (std::size_t i = 0; i < 2352U; i += 2U) {
        bin[2352U + i] = 0x00;
        bin[2352U + i + 1U] = 0x40; // little-endian 0x4000
    }
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    REQUIRE(disc.has_value());

    segacd_adapter adapter(make_bios());
    auto& sub = *adapter.machine().sub;
    sub.attach_disc(&*disc); // attach resets CD-DA state...
    sub.cdda_active = true;  // ...so arm it afterwards, over the PCM sector
    sub.cdda_start_lba = 1;
    sub.cdda_end_lba = 1;
    sub.cdda_current_lba = 1;
    sub.cdda_sample_in_sector = 0;

    const auto chunk = adapter.drain_audio();
    REQUIRE(chunk.frame_count > 0U);
    bool nonzero = false;
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        if (chunk.samples[i] != 0) {
            nonzero = true;
            break;
        }
    }
    REQUIRE(nonzero); // CD-DA reached the mixed output
}

// Data-gated: point MNEMOS_SEGACD_BIOS at a real Sega CD / Mega CD BIOS ROM to
// boot it for real (no disc -> the BIOS's CD-player / no-disc screen). Skipped
// when the env var is unset so the default suite stays hermetic.
TEST_CASE("segacd_adapter boots a real Sega CD BIOS", "[segacd][adapter][.bios]") {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    const char* bios_path = std::getenv("MNEMOS_SEGACD_BIOS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (bios_path == nullptr) {
        SUCCEED("MNEMOS_SEGACD_BIOS not set -- skipping real-BIOS boot");
        return;
    }

    std::ifstream is(bios_path, std::ios::binary);
    REQUIRE(is.good());
    std::vector<std::uint8_t> bios((std::istreambuf_iterator<char>(is)),
                                   std::istreambuf_iterator<char>());
    REQUIRE(bios.size() >= 0x20000U);

    // Optionally mount a real game disc (MNEMOS_SEGACD_DISC = a .cue/.iso) to
    // observe the BIOS's disc-boot sequence.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* disc_path = std::getenv("MNEMOS_SEGACD_DISC");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (disc_path == nullptr) {
        SUCCEED("MNEMOS_SEGACD_DISC not set -- the disc-boot handshake needs a disc");
        return;
    }
    segacd_adapter adapter(std::move(bios), {}, "", nullptr, disc_path);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const bool reinit_trace = std::getenv("MNEMOS_SEGACD_REINIT") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (reinit_trace) {
        // The re-init ($206) is reached from $40C when the status flag at PRG
        // $5A2E reads $FF (error). Trace who writes $5A2E and with what value.
        auto* sub = adapter.machine().sub.get();
        sub->sub_bus.set_access_observer([sub](const auto& ev) {
            if (ev.write && ev.address >= 0x5870U && ev.address <= 0x5877U) {
                static int n = 0;
                if (n++ < 60) {
                    std::fprintf(stderr, "[wr] [%04X]=%02X pc=%06X\n", ev.address, ev.value,
                                 sub->sub_cpu.cpu_registers().pc);
                }
            }
        });
    }
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const bool pchist_trace = std::getenv("MNEMOS_SEGACD_PCHIST") != nullptr;
    // Dump the full sub-CPU PC stream (3-byte LE per instruction) to a file, matching
    // the reference emulator's headless trace format, for the differential boot trace.
    const char* subtrace_path = std::getenv("MNEMOS_SEGACD_SUBTRACE");
    std::FILE* subtrace = (subtrace_path != nullptr) ? std::fopen(subtrace_path, "wb") : nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    // Sub-CPU PC histogram: pinpoints the loop the sub spins in (e.g. the stuck
    // main<->sub comm handshake). Also captures the PC path into the LAST entry of
    // the $618E comm-sync routine (a 64-PC ring snapshotted on each $618E hit) to
    // reveal which branch diverts the sub into the comm-wait instead of a disc read.
    std::unordered_map<std::uint32_t, std::uint64_t> sub_pc_hist;
    std::unordered_map<std::uint32_t, std::uint64_t> main_pc_hist;
    std::array<std::uint32_t, 64> pc_ring{};
    std::array<std::uint32_t, 64> pc_path{};
    std::size_t pc_ring_idx = 0;
    bool pc_path_captured = false;
    std::array<std::uint32_t, 64> main_pc_ring{};
    std::array<std::uint32_t, 64> main_pc_path{};
    std::size_t main_ring_idx = 0;
    bool main_path_captured = false;
    int rt_frame = 0; // current frame, for gating the round-latency stamps
    std::uint8_t wp_5837 = adapter.machine().sub->prg_ram[0x5837U]; // op-accept latch ($5837.0)
    std::uint8_t wp_583b = adapter.machine().sub->prg_ram[0x583BU]; // op-active flag ($583B.7)
    std::uint8_t wp_583e =
        adapter.machine().sub->prg_ram[0x583EU]; // op-arm flags ($583E.1 gates $62F4)
    // The comm-driver dispatcher's command word; cmd $10 -> the $621E op-arm handler.
    std::uint16_t wp_582e = static_cast<std::uint16_t>(
        (adapter.machine().sub->prg_ram[0x582EU] << 8) | adapter.machine().sub->prg_ram[0x582FU]);
    // The CDBSTAT status word the comm cycle posts; its $E000 bits gate the
    // $62F4 handshake-advance arm consumption.
    std::uint16_t wp_5844 = static_cast<std::uint16_t>(
        (adapter.machine().sub->prg_ram[0x5844U] << 8) | adapter.machine().sub->prg_ram[0x5845U]);
    // The BIOS status word itself (CDBSTAT struct word 0): $8000/$2000 = op-busy
    // bits; the writer PCs reveal the op-completion code + its gating condition.
    std::uint16_t wp_5e80 = static_cast<std::uint16_t>(
        (adapter.machine().sub->prg_ram[0x5E80U] << 8) | adapter.machine().sub->prg_ram[0x5E81U]);
    if (pchist_trace || subtrace != nullptr) {
        adapter.machine().sub->sub_cpu.diagnostics().set_trace_callback([&](std::uint32_t pc) {
            ++sub_pc_hist[pc];
            if (subtrace != nullptr) {
                const unsigned char b[3] = {static_cast<unsigned char>(pc & 0xFFU),
                                            static_cast<unsigned char>((pc >> 8U) & 0xFFU),
                                            static_cast<unsigned char>((pc >> 16U) & 0xFFU)};
                std::fwrite(b, 1U, 3U, subtrace);
            }
            const std::uint8_t v5837 = adapter.machine().sub->prg_ram[0x5837U];
            if (v5837 != wp_5837) { // who sets the op-accept latch $5837 (bit0 arms the disc op)?
                std::fprintf(stderr, "[wp5837] sub pc=%06X $5837 %02X->%02X\n", pc, wp_5837, v5837);
                wp_5837 = v5837;
            }
            const std::uint8_t v583b = adapter.machine().sub->prg_ram[0x583BU];
            if (v583b != wp_583b) { // who sets the op flag $583B (bit7 = operation active)?
                std::fprintf(stderr, "[wp583B] sub pc=%06X $583B %02X->%02X\n", pc, wp_583b, v583b);
                wp_583b = v583b;
            }
            const std::uint8_t v583e = adapter.machine().sub->prg_ram[0x583EU];
            if (v583e != wp_583e) { // who arms $583E (bit1 gates the $62F4 handshake advance)?
                std::fprintf(stderr, "[wp583E] sub pc=%06X $583E %02X->%02X\n", pc, wp_583e, v583e);
                wp_583e = v583e;
            }
            const std::uint16_t v582e =
                static_cast<std::uint16_t>((adapter.machine().sub->prg_ram[0x582EU] << 8) |
                                           adapter.machine().sub->prg_ram[0x582FU]);
            if (v582e != wp_582e) { // the dispatcher's command word (cmd $10 -> $621E)
                std::fprintf(stderr, "[wp582E] sub pc=%06X $582E %04X->%04X\n", pc, wp_582e, v582e);
                wp_582e = v582e;
            }
            const std::uint16_t v5844 =
                static_cast<std::uint16_t>((adapter.machine().sub->prg_ram[0x5844U] << 8) |
                                           adapter.machine().sub->prg_ram[0x5845U]);
            if (v5844 != wp_5844) { // the posted CDBSTAT word ($E000 = busy bits)
                std::fprintf(stderr, "[wp5844] sub pc=%06X $5844 %04X->%04X\n", pc, wp_5844, v5844);
                wp_5844 = v5844;
            }
            const std::uint16_t v5e80 =
                static_cast<std::uint16_t>((adapter.machine().sub->prg_ram[0x5E80U] << 8) |
                                           adapter.machine().sub->prg_ram[0x5E81U]);
            if (v5e80 != wp_5e80) { // the BIOS status word (op-busy bits live here)
                std::fprintf(stderr, "[wp5E80] sub pc=%06X $5E80 %04X->%04X\n", pc, wp_5e80, v5e80);
                wp_5e80 = v5e80;
            }
            static std::uint8_t wp_5878 = 0xEE; // validated drive-status mirror ($11B6 reads it)
            const std::uint8_t v5878 = adapter.machine().sub->prg_ram[0x5878U];
            if (v5878 != wp_5878) {
                std::fprintf(stderr, "[wp5878] sub pc=%06X $5878 %02X->%02X\n", pc, wp_5878, v5878);
                wp_5878 = v5878;
            }
            static std::uint16_t wp_5afc = 0xEEEE; // the CD-driver status word itself
            const std::uint16_t v5afc =
                static_cast<std::uint16_t>((adapter.machine().sub->prg_ram[0x5AFCU] << 8) |
                                           adapter.machine().sub->prg_ram[0x5AFDU]);
            if (v5afc != wp_5afc) {
                std::fprintf(stderr, "[wp5AFC] sub pc=%06X $5AFC %04X->%04X\n", pc, wp_5afc, v5afc);
                wp_5afc = v5afc;
            }
            static std::uint16_t wp_5b0a = 0xEEEE; // CD-driver state index
            const std::uint16_t v5b0a =
                static_cast<std::uint16_t>((adapter.machine().sub->prg_ram[0x5B0AU] << 8) |
                                           adapter.machine().sub->prg_ram[0x5B0BU]);
            if (v5b0a != wp_5b0a) {
                std::fprintf(stderr, "[wp5B0A] sub pc=%06X $5B0A %04X->%04X\n", pc, wp_5b0a, v5b0a);
                wp_5b0a = v5b0a;
            }
            // Comm-cycle register snapshot: a6 = the comm driver's work base (the
            // $3E(a6) arm-consume target), a3 = the posting queue, d0 = the posted
            // status word whose $E000 busy bits gate the $62F4 advance.
            if (pc == 0x0062DCU) { // the comm cycle just popped the word it will post
                static std::uint32_t last_d0 = 0xEEEEEEEEU;
                const auto& r = adapter.machine().sub->sub_cpu.cpu_registers();
                const std::uint32_t d0 = r.d[0] & 0xFFFFU;
                if (d0 != last_d0) {
                    last_d0 = d0;
                    std::fprintf(stderr, "[commpost] d0=%04X arm9826=%02X\n", d0,
                                 adapter.machine().sub->prg_ram[0x9826U]);
                }
            }
            if (pc == 0x002362U && rt_frame >= 120 && rt_frame < 200) {
                // Just returned from $74E (the queue put): d1 = verdict, and the
                // 'CDCD' queue descriptor lives at $5A6E.
                static int qput = 0;
                if (qput++ < 12) {
                    const auto& r = adapter.machine().sub->sub_cpu.cpu_registers();
                    const auto* q = adapter.machine().sub->prg_ram.data() + 0x5A6EU;
                    std::fprintf(stderr,
                                 "[qput] d1=%04X q5A6E=%02X%02X%02X%02X %02X%02X%02X%02X "
                                 "%02X%02X%02X%02X %02X%02X%02X%02X\n",
                                 r.d[1] & 0xFFFFU, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7],
                                 q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15]);
                }
            }
            if (pc == 0x006274U) { // a0 = the CDBSTAT struct the comm cycle posts from
                static int cdbstat_logged = 0;
                if (cdbstat_logged < 8) {
                    ++cdbstat_logged;
                    const auto& r = adapter.machine().sub->sub_cpu.cpu_registers();
                    std::fprintf(stderr, "[cdbstat] a0=%06X\n", r.a[0]);
                }
            }
            pc_ring[pc_ring_idx % pc_ring.size()] = pc;
            ++pc_ring_idx;
            if (pc == 0x618EU) {
                for (std::size_t i = 0; i < pc_ring.size(); ++i) {
                    pc_path[i] = pc_ring[(pc_ring_idx + i) % pc_ring.size()];
                }
                pc_path_captured = true;
            }
            // Round-latency timeline (SUB side): stamp the comm-cycle phase writers
            // and the CDC sweep start in MAIN-cycle terms so one load-loop
            // iteration reads as a single clock-aligned timeline.
            if (rt_frame >= 120 && rt_frame < 200 && (pc == 0x1EF2U)) {
                static int round_ev = 0;
                if (round_ev++ < 400) {
                    std::fprintf(stderr, "[rt] sub %04X M=%llu dec#%llu\n", pc,
                                 static_cast<unsigned long long>(
                                     adapter.machine().genesis->cpu.elapsed_cycles()),
                                 static_cast<unsigned long long>(
                                     adapter.machine().sub->cdc_sectors_decoded));
                }
            }
            // Who arms/disarms the CDC service ($5A30 bit 7)?
            static std::uint8_t wp_5a30 = 0xEE;
            const std::uint8_t v5a30 = adapter.machine().sub->prg_ram[0x5A30U];
            if (v5a30 != wp_5a30 && rt_frame >= 120 && rt_frame < 200) {
                std::fprintf(stderr, "[wp5A30] sub pc=%06X $5A30 %02X->%02X M=%llu\n", pc, wp_5a30,
                             v5a30,
                             static_cast<unsigned long long>(
                                 adapter.machine().genesis->cpu.elapsed_cycles()));
            }
            wp_5a30 = v5a30;
            // The read-driver error-flag byte: each checker sets a distinct bit;
            // the writer PC names the failing validation directly.
            static std::uint8_t wp_5a31 = 0xEE;
            const std::uint8_t v5a31 = adapter.machine().sub->prg_ram[0x5A31U];
            if (v5a31 != wp_5a31 && rt_frame >= 120 && rt_frame < 200) {
                std::fprintf(stderr, "[wp5A31] sub pc=%06X $5A31 %02X->%02X\n", pc, wp_5a31, v5a31);
            }
            wp_5a31 = v5a31;
        });
        // Main-CPU PC histogram: pinpoints the BootROM loop the main spins in while
        // it fails to post the CDBIOS disc-read command to gate comm words $10-$1F.
        adapter.machine().genesis->cpu.diagnostics().set_trace_callback([&](std::uint32_t pc) {
            ++main_pc_hist[pc];
            // Round-latency timeline (MAIN side): stamp the $1288/$1290 wait entry
            // and exit plus the pump entry ($1252), in elapsed main cycles.
            {
                static bool in_wait = false;
                static int main_ev = 0;
                const bool at_wait =
                    (pc == 0x1288U || pc == 0x128CU || pc == 0x1290U || pc == 0x1294U);
                if (rt_frame >= 120 && rt_frame < 200 && at_wait != in_wait && main_ev < 260) {
                    ++main_ev;
                    std::fprintf(stderr, "[rt] main %s pc=%06X M=%llu\n",
                                 at_wait ? "wait+" : "wait-", pc,
                                 static_cast<unsigned long long>(
                                     adapter.machine().genesis->cpu.elapsed_cycles()));
                }
                in_wait = at_wait;
                if (rt_frame >= 120 && rt_frame < 200 && pc == 0x1252U && main_ev < 260) {
                    ++main_ev;
                    std::fprintf(stderr, "[rt] main pump M=%llu\n",
                                 static_cast<unsigned long long>(
                                     adapter.machine().genesis->cpu.elapsed_cycles()));
                }
            }
            // The BIOS round-phase byte ($FDDE, bchg #0 per comm round): its
            // toggle sequence vs the comm edges exposes a lost-round parity break.
            static std::uint8_t wp_fdde = 0xEE;
            const std::uint8_t v = adapter.machine().genesis->work_ram[0xFDDEU];
            if (v != wp_fdde) {
                std::fprintf(stderr, "[wpFDDE] main pc=%06X $FDDE %02X->%02X\n", pc, wp_fdde, v);
                wp_fdde = v;
            }
            main_pc_ring[main_ring_idx % main_pc_ring.size()] = pc;
            ++main_ring_idx;
            if (pc == 0x000A0CU) { // the logo/delay sub entry -- capture its caller
                for (std::size_t i = 0; i < main_pc_ring.size(); ++i) {
                    main_pc_path[i] = main_pc_ring[(main_ring_idx + i) % main_pc_ring.size()];
                }
                main_path_captured = true;
            }
        });
    }
    // ~10 s of emulated boot by default; MNEMOS_SEGACD_FRAMES overrides for
    // long-horizon runs (game IP load + title).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* frames_env = std::getenv("MNEMOS_SEGACD_FRAMES");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    const int kBootFrames = (frames_env != nullptr) ? std::atoi(frames_env) : 600;
    for (int i = 0; i < kBootFrames; ++i) {
        if (i == 450) { // tail-window the histograms: what still RUNS while parked?
            sub_pc_hist.clear();
            main_pc_hist.clear();
        }
        rt_frame = i;
        // Optional headless START press (MNEMOS_SEGACD_PRESS_START = frame#):
        // held for 10 frames, e.g. to drive the BIOS "Press the START BUTTON"
        // prompt of the disc-boot flow.
        {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
            static const char* press_env = std::getenv("MNEMOS_SEGACD_PRESS_START");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (press_env != nullptr) {
                const int press_at = std::atoi(press_env);
                mnemos::peripheral::controller_state pad{};
                pad.start = (i >= press_at && i < press_at + 10);
                adapter.apply_input(0, pad);
            }
        }
        if (pchist_trace && i >= 100 && i < 220) {
            std::fprintf(
                stderr, "[frame] %d M=%llu\n", i,
                static_cast<unsigned long long>(adapter.machine().genesis->cpu.elapsed_cycles()));
        }
        adapter.step_one_frame();
    }

    const auto& sub = *adapter.machine().sub;
    std::fprintf(stderr, "[segacd-boot] frames=%llu sub_reset=%d sub_busreq=%d sub_elapsed=%llu\n",
                 static_cast<unsigned long long>(adapter.frames_stepped()),
                 static_cast<int>(sub.sub_reset_asserted), static_cast<int>(sub.sub_busreq),
                 static_cast<unsigned long long>(sub.sub_cpu.elapsed_cycles()));
    std::fprintf(stderr, "[segacd-boot] gate $00-$0F:");
    for (std::size_t i = 0; i < 16; ++i) {
        std::fprintf(stderr, " %02X", sub.gate_array[i]);
    }
    std::fprintf(
        stderr, "\n[segacd-boot] cdd_loaded=%d cdd_drive_status=%02X cdd_lba=%d cdc_decoded=%llu\n",
        static_cast<int>(sub.cdd_loaded), sub.cdd_drive_status, sub.cdd_lba,
        static_cast<unsigned long long>(sub.cdc_sectors_decoded));
    std::fprintf(stderr, "[segacd-boot] main_pc=%06X main_elapsed=%llu sub_pc=%06X sub_mask=%02X\n",
                 adapter.machine().genesis->cpu.cpu_registers().pc,
                 static_cast<unsigned long long>(adapter.machine().genesis->cpu.elapsed_cycles()),
                 adapter.machine().sub->sub_cpu.cpu_registers().pc, sub.sub_irq_mask);
    // Why is the parked main not taking VBlank? SR.IPL + the VDP's IE0 tell.
    std::fprintf(stderr, "[segacd-boot] main_sr=%04X vdp_reg0=%02X vdp_reg1=%02X\n",
                 adapter.machine().genesis->cpu.cpu_registers().sr,
                 adapter.machine().genesis->vdp.reg(0), adapter.machine().genesis->vdp.reg(1));
    // Main BootROM CD-driver state ($FFFDDC region): $FDDC bit7 = the $1D62 busy
    // wait the main spins on; $FDDE bit1 = CD-op active; $FDDF bit2 = busy; $FDF0 =
    // the $1480 dispatch function index. Reveals whether the main ever issues a read.
    std::fprintf(stderr, "[segacd-boot] main CD-state $FDDC-$FDF3:");
    for (std::size_t i = 0xFDDCU; i <= 0xFDF3U; ++i) {
        std::fprintf(stderr, " %02X", adapter.machine().genesis->work_ram[i]);
    }
    std::fprintf(stderr, "\n");

    if (subtrace != nullptr) {
        std::fclose(subtrace);
        std::fprintf(stderr, "[subtrace] wrote sub-CPU PC stream to %s\n", subtrace_path);
    }
    if (pchist_trace) {
        std::vector<std::pair<std::uint32_t, std::uint64_t>> top(sub_pc_hist.begin(),
                                                                 sub_pc_hist.end());
        std::sort(top.begin(), top.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::fprintf(stderr, "[pchist] %zu distinct sub PCs; top 28 (hottest = stuck loop):\n",
                     top.size());
        for (std::size_t i = 0; i < top.size() && i < 28U; ++i) {
            std::fprintf(stderr, "  $%06X  %llu\n", top[i].first,
                         static_cast<unsigned long long>(top[i].second));
        }
        if (pc_path_captured) {
            std::fprintf(
                stderr, "[pcpath] sub PCs into the last $618E comm-sync entry (oldest->newest):\n");
            for (std::size_t i = 0; i < pc_path.size(); ++i) {
                std::fprintf(stderr, " %06X", pc_path[i]);
                if ((i % 8U) == 7U) {
                    std::fprintf(stderr, "\n");
                }
            }
        }
        std::vector<std::pair<std::uint32_t, std::uint64_t>> mtop(main_pc_hist.begin(),
                                                                  main_pc_hist.end());
        std::sort(mtop.begin(), mtop.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::fprintf(stderr, "[mainpc] %zu distinct main PCs; top 24 (the BootROM wait loop):\n",
                     mtop.size());
        for (std::size_t i = 0; i < mtop.size() && i < 24U; ++i) {
            std::fprintf(stderr, "  $%06X  %llu\n", mtop[i].first,
                         static_cast<unsigned long long>(mtop[i].second));
        }
        if (main_path_captured) {
            std::fprintf(stderr, "[mainpath] main PCs into the last $0A0C logo-loop entry:\n");
            for (std::size_t i = 0; i < main_pc_path.size(); ++i) {
                std::fprintf(stderr, " %06X", main_pc_path[i]);
                if ((i % 8U) == 7U) {
                    std::fprintf(stderr, "\n");
                }
            }
        }
    }

    const auto fb = adapter.current_frame();
    // Optional end-of-run framebuffer dump (MNEMOS_SEGACD_FBDUMP = .ppm path)
    // for headless visual verification of the boot stage reached.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* fb_dump = std::getenv("MNEMOS_SEGACD_FBDUMP");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (fb_dump != nullptr && fb.pixels != nullptr) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::fopen: opt-in diagnostic dump
#endif
        std::FILE* f = std::fopen(fb_dump, "wb");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (f != nullptr) {
            std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
            const std::uint32_t stride = fb.effective_stride();
            for (std::uint32_t y = 0; y < fb.height; ++y) {
                for (std::uint32_t x = 0; x < fb.width; ++x) {
                    const std::uint32_t p = fb.pixels[y * stride + x];
                    const unsigned char rgb[3] = {static_cast<unsigned char>((p >> 16) & 0xFF),
                                                  static_cast<unsigned char>((p >> 8) & 0xFF),
                                                  static_cast<unsigned char>(p & 0xFF)};
                    std::fwrite(rgb, 1, 3, f);
                }
            }
            std::fclose(f);
        }
    }
    std::size_t nonzero = 0;
    if (fb.pixels != nullptr) {
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                if ((fb.pixels[y * stride + x] & 0x00FFFFFFU) != 0U) {
                    ++nonzero;
                }
            }
        }
    }
    std::fprintf(stderr, "[segacd-boot] fb %ux%u, %zu non-black px\n", fb.width, fb.height,
                 nonzero);

    // Optionally dump PRG-RAM (the sub-CPU BIOS the main loaded there + the CDBIOS
    // work area at $97E8) to a file for disassembly (MNEMOS_SEGACD_PRGDUMP=path).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* prg_dump = std::getenv("MNEMOS_SEGACD_PRGDUMP");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (prg_dump != nullptr) {
        std::ofstream os(prg_dump, std::ios::binary);
        os.write(reinterpret_cast<const char*>(sub.prg_ram.data()),
                 static_cast<std::streamsize>(sub.prg_ram.size()));
    }

    // The BIOS boots AND completes its main<->sub handshake: the main 68000
    // drives the gate array, the sub-CPU runs valid PRG-RAM code (a 24-bit PC),
    // it SIGNALS the main by setting gate $0F bit 6 (the sub-ready flag the main
    // spins on at BIOS $132C), and the main then ADVANCES past that poll. With a
    // disc mounted the BIOS renders its "(c) 1992 SEGA" logo earlier, then clears
    // it for the disc-load phase -- so the framebuffer is intentionally not
    // asserted non-black at this later frame.
    bool gate_written = false;
    for (std::size_t i = 0; i < sub.gate_array.size(); ++i) {
        if (sub.gate_array[i] != 0U) {
            gate_written = true;
            break;
        }
    }
    REQUIRE(gate_written);                                  // BIOS drove the gate array
    REQUIRE(sub.sub_cpu.cpu_registers().pc <= 0x00FFFFFFU); // sub on a valid bus address
    REQUIRE(adapter.machine().genesis->cpu.cpu_registers().pc != 0x00132CU); // main past early boot
    if (disc_path != nullptr) {
        // With a disc the boot advances past the word-RAM handover deadlock: the sub
        // clears its $0F.6 + passes the $6194 comm-sync, and the main clears its
        // $0E.2 request + advances past the RET-wait at $1AFA.
        REQUIRE(sub.sub_cpu.cpu_registers().pc != 0x006194U); // sub past comm-sync
        REQUIRE(adapter.machine().genesis->cpu.cpu_registers().pc !=
                0x001AFAU); // main past RET deadlock
    }
    REQUIRE(adapter.frames_stepped() ==
            static_cast<std::uint64_t>(static_cast<unsigned>(kBootFrames)));
    (void)nonzero;
}
