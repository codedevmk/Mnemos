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

    segacd_adapter adapter(std::move(bios));
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
    std::fprintf(stderr, "\n[segacd-boot] cdd_drive_status=%02X cdd_lba=%d\n", sub.cdd_drive_status,
                 sub.cdd_lba);

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
    std::fprintf(stderr, "[segacd-boot] fb %ux%u, %zu non-black px\n", fb.width, fb.height, nonzero);

    // Observed-true smoke signals: the main 68000 runs the BIOS (it writes the
    // gate array to talk to the sub side) and the CDD reports no-disc (0x0B,
    // since we booted with no disc). NOTE: the BIOS does not yet reach its
    // visible screen -- it stalls polling, the suspected cause being the
    // not-yet-implemented sub-CPU timer interrupt (INT3). Asserting a non-blank
    // framebuffer is deferred until that lands.
    bool gate_written = false;
    for (std::size_t i = 0; i < sub.gate_array.size(); ++i) {
        if (sub.gate_array[i] != 0U) {
            gate_written = true;
            break;
        }
    }
    REQUIRE(gate_written);                 // the BIOS executed + drove the gate array
    REQUIRE(sub.cdd_drive_status == 0x0BU); // CDD no-disc status
    REQUIRE(adapter.frames_stepped() == static_cast<std::uint64_t>(kBootFrames));
}
