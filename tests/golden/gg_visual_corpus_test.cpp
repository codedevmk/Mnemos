// Game Gear visual-golden corpus (data-gated): renders a curated set of titles
// and asserts the visible (160x144) framebuffer hashes match locked goldens.
// This is the enforced pixel check the boot tests lacked -- the exact gap that
// let the horizontal-scroll (#51) and sprite-pattern (#52) bugs ship.
//
// IMPORTANT -- these are a REGRESSION RATCHET, not reference-derived ground
// truth: each hash is Mnemos's own output for a frame that was visually verified
// correct on 2026-06-04 (rendered to PNG and inspected by eye). They catch any
// future change that alters these screens; they do not prove bit-accuracy vs
// hardware. Reference-derived goldens (e.g. cross-checked against an independent
// emulator) would carry a different, stronger guarantee.
//
// Data-gated: set MNEMOS_GG_CORPUS to a directory of raw .gg files. Each entry is
// matched to a file by ROM CRC-32 (identity, not path); absent ROMs are skipped.

#include "cli_args.hpp" // press_event + input_for_frame (port-1 scripted input)
#include "crc32.hpp"
#include "scheduler.hpp"
#include "sha256.hpp"
#include "sms_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::apps::player::adapters::input_for_frame;
    using mnemos::apps::player::adapters::press_event;
    using mnemos::manifests::sms::assemble_sms;

    std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
        char* buf = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
            return std::nullopt;
        }
        std::string v(buf);
        std::free(buf);
        return v;
#else
        const char* v = std::getenv(name);
        return v ? std::optional<std::string>(v) : std::nullopt;
#endif
    }

    std::optional<std::vector<std::uint8_t>> read_file(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }

    // SHA-256 of the visible LCD region, packed row-by-row (the GG view is strided
    // -- 160 visible columns over the VDP's 256-pixel pitch).
    std::string visible_hash(const mnemos::chips::frame_buffer_view& fb) {
        std::vector<std::uint8_t> bytes;
        const std::uint32_t stride = fb.effective_stride();
        bytes.reserve(static_cast<std::size_t>(fb.width) * fb.height * 3U);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                const std::uint32_t px = row[x];
                bytes.push_back(static_cast<std::uint8_t>(px >> 16U));
                bytes.push_back(static_cast<std::uint8_t>(px >> 8U));
                bytes.push_back(static_cast<std::uint8_t>(px));
            }
        }
        return mnemos::security::cryptography::sha256(bytes).hex();
    }

    struct golden_entry {
        std::uint32_t crc;
        std::string label;
        std::uint64_t frames;
        std::vector<press_event> presses; // port-1 scripted input (empty = none)
        std::string sha256;               // locked ratchet hash ("" = not yet locked)
    };

    // The curated set, each visually verified correct on 2026-06-04. All reach a
    // stable, content-rich screen without input (titles / auto-demo gameplay), so
    // `presses` is empty; press-driven screens are added once verified.
    std::vector<golden_entry> corpus() {
        return {
            {0x83FA26D9U,
             "Columns (title)",
             500U,
             {},
             "98bf3c76f07ba30d66d7db4e32b9f90b628ddf6784c2220962adee3f86ca1582"},
            {0xF95BBD91U,
             "Sonic Chaos (title)",
             700U,
             {},
             "541d641f2d892377f359876bb0f7bf8eead01e61a237508c7259af25d9326943"},
            {0x8D8BFDC4U,
             "Baku Baku Animal (title)",
             800U,
             {},
             "15180ad4abb9d1390ccc5ce58303b444589b06e069c1aa89f4b071ba312adee4"},
            {0x5BB6E5D6U,
             "Tails Adventure (map)",
             1500U,
             {},
             "e5611b8174df269fa36cee8e211a5d58c922b89a956bae75a0abac17216baafa"},
            {0x15AD37A5U,
             "Sonic 2 auto-demo (scroll+sprites)",
             1800U,
             {},
             "b2bcd834e0bca0c97a4241f5850885c56049ccac212790da834c4790bf9624d4"},
            {0xA9210434U,
             "Sonic Spinball (title sprites)",
             900U,
             {},
             "02ad7d759c42b92656fef39e118d28f2de09961a8c4e58ef1e983a37e172dc89"},
        };
    }

    std::string render_visible_hash(const std::vector<std::uint8_t>& rom, const golden_entry& e) {
        auto sys = assemble_sms(rom, {.game_gear = true});
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        if (e.presses.empty()) {
            sched.run_frames(e.frames); // canonical path (matches gg_boot_test)
        } else {
            for (std::uint64_t f = 1; f <= e.frames; ++f) {
                const auto in = input_for_frame(e.presses, f);
                if (auto* dev = sys->port_device(0)) {
                    dev->apply_state(in);
                }
                sys->set_gg_start(in.start);
                sched.run_frames(1); // step one frame with this frame's input
            }
        }
        return visible_hash(sys->vdp.framebuffer());
    }
} // namespace

TEST_CASE("gg visual golden corpus", "[golden][gg][corpus]") {
    const auto dir = get_env("MNEMOS_GG_CORPUS");
    if (!dir) {
        SKIP("set MNEMOS_GG_CORPUS to a directory of raw .gg files");
    }

    // Index the corpus directory by ROM CRC-32.
    std::unordered_map<std::uint32_t, fs::path> by_crc;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(*dir, ec), end; it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file()) {
            continue;
        }
        if (it->path().extension() == ".gg") {
            if (auto bytes = read_file(it->path())) {
                by_crc[mnemos::security::cryptography::crc32(*bytes)] = it->path();
            }
        }
    }

    int ran = 0;
    for (const auto& e : corpus()) {
        const auto found = by_crc.find(e.crc);
        if (found == by_crc.end()) {
            WARN("absent (skipped): " << e.label);
            continue;
        }
        auto rom = read_file(found->second);
        REQUIRE(rom.has_value());
        const std::string hash = render_visible_hash(*rom, e);
        ++ran;
        INFO("entry: " << e.label << "  computed=" << hash);
        if (e.sha256.empty()) {
            WARN("no golden locked for " << e.label << "; computed=" << hash);
        } else {
            CHECK(hash == e.sha256);
        }
    }
    if (ran == 0) {
        WARN("MNEMOS_GG_CORPUS set but no curated ROMs matched by CRC");
    }
}
