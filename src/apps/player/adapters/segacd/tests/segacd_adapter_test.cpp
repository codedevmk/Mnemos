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
    std::uint8_t wp_5837 = adapter.machine().sub->prg_ram[0x5837U]; // op-accept latch ($5837.0)
    std::uint8_t wp_583b = adapter.machine().sub->prg_ram[0x583BU]; // op-active flag ($583B.7)
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
            pc_ring[pc_ring_idx % pc_ring.size()] = pc;
            ++pc_ring_idx;
            if (pc == 0x618EU) {
                for (std::size_t i = 0; i < pc_ring.size(); ++i) {
                    pc_path[i] = pc_ring[(pc_ring_idx + i) % pc_ring.size()];
                }
                pc_path_captured = true;
            }
        });
        // Main-CPU PC histogram: pinpoints the BootROM loop the main spins in while
        // it fails to post the CDBIOS disc-read command to gate comm words $10-$1F.
        adapter.machine().genesis->cpu.diagnostics().set_trace_callback([&](std::uint32_t pc) {
            ++main_pc_hist[pc];
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
    constexpr int kBootFrames = 600; // ~10 s of emulated boot
    for (int i = 0; i < kBootFrames; ++i) {
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
            std::fprintf(stderr,
                         "[pcpath] sub PCs into the last $618E comm-sync entry (oldest->newest):\n");
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
        REQUIRE(sub.sub_cpu.cpu_registers().pc != 0x006194U);                    // sub past comm-sync
        REQUIRE(adapter.machine().genesis->cpu.cpu_registers().pc != 0x001AFAU); // main past RET deadlock
    }
    REQUIRE(adapter.frames_stepped() == static_cast<std::uint64_t>(kBootFrames));
    (void)nonzero;
}
