// Phase D2: the segacd_adapter drives a full Sega CD frame -- the Genesis
// scheduler runs the main 68000 (which boots the BIOS), and the per-frame
// accumulator runs the sub-CPU + CD frames. A synthetic BIOS marks work RAM and
// releases the sub-CPU so we can see both halves advance.

#include "segacd_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <system_error>
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
    segacd_adapter adapter(std::move(bios), {}, "", nullptr, disc_path != nullptr ? disc_path : "");
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

    // Optionally dump the first 32 KB of PRG-RAM (the sub-CPU BIOS the main
    // loaded there) to a file for disassembly (MNEMOS_SEGACD_PRGDUMP=path).
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
                 static_cast<std::streamsize>(0x8000));
    }

    // The BIOS boots and renders its screen: the main 68000 drives the gate
    // array, the sub-CPU runs valid PRG-RAM code (a 24-bit PC, not a crashed
    // out-of-range address), and the BIOS draws (a non-trivial slice of the
    // framebuffer is non-black -- the "(c) 1992 SEGA" boot animation).
    bool gate_written = false;
    for (std::size_t i = 0; i < sub.gate_array.size(); ++i) {
        if (sub.gate_array[i] != 0U) {
            gate_written = true;
            break;
        }
    }
    REQUIRE(gate_written);                                  // BIOS drove the gate array
    REQUIRE(sub.sub_cpu.cpu_registers().pc <= 0x00FFFFFFU); // sub on a valid bus address
    REQUIRE(nonzero > 1000U);                               // BIOS drew its boot screen
    REQUIRE(adapter.frames_stepped() == static_cast<std::uint64_t>(kBootFrames));
}
