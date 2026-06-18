// Adapter smoke + media wiring. The C64 player_system adapter is a shell over
// build_c64_runtime() + a scheduler. These boot it on synthetic zero-filled
// system ROMs (the 6510 runs from a null reset vector, like the manifest
// parity test's always-on path) and exercise the player_system contract,
// media routing, multi-disk swapping, and the autostart key translation --
// none of which need a real ROM set. (End-to-end LOAD/RUN does, and is gated
// on $MNEMOS_C64_ROM_DIR elsewhere.)

#include "c64_adapter.hpp"

#include "d64_image.hpp"
#include "prg_disk.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::c64::ascii_to_chord;
    using mnemos::apps::player::adapters::c64::c64_adapter;
    using mnemos::manifests::c64::c64_config;
    using key = mnemos::manifests::c64::c64_input::key;

    std::vector<std::uint8_t> basic_rom() { return std::vector<std::uint8_t>(0x2000U, 0x00U); }
    std::vector<std::uint8_t> kernal_rom() { return std::vector<std::uint8_t>(0x2000U, 0x00U); }
    std::vector<std::uint8_t> chargen_rom() { return std::vector<std::uint8_t>(0x1000U, 0x00U); }

    // Bare machine, no media.
    c64_adapter bare(c64_config config = {}) {
        return c64_adapter(basic_rom(), kernal_rom(), chargen_rom(), {}, {}, true, config);
    }

    std::vector<std::uint8_t> sample_prg() {
        std::vector<std::uint8_t> prg = {0x01U, 0x08U};
        for (int i = 0; i < 64; ++i) {
            prg.push_back(static_cast<std::uint8_t>(i));
        }
        return prg;
    }

} // namespace

TEST_CASE("c64_adapter advances frames and reports region") {
    c64_adapter adapter = bare({.video_region = c64_config::region::pal});

    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
    CHECK(adapter.frames_stepped() == 0U);

    // The VIC-II sizes its framebuffer lazily on the first rendered line, so a
    // complete frame only exists after the first step.
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);

    const auto fb = adapter.current_frame();
    CHECK(fb.width > 0U);
    CHECK(fb.height > 0U);
}

TEST_CASE("c64_adapter selects NTSC pacing when configured") {
    c64_adapter adapter = bare({.video_region = c64_config::region::ntsc});
    CHECK(adapter.region().frames_per_second_x1000 == 60000U);
}

TEST_CASE("c64_adapter enumerates its chips in scheduler order") {
    c64_adapter adapter = bare();
    const auto chips = adapter.chips();
    REQUIRE(chips.size() == 5U);
    for (auto* c : chips) {
        CHECK(c != nullptr);
    }
}

TEST_CASE("c64_adapter publishes system RAM memory views") {
    c64_adapter adapter = bare();

    const auto views = adapter.memory_views();
    REQUIRE(views.size() == 2U);
    REQUIRE(views[0] != nullptr);
    REQUIRE(views[1] != nullptr);
    CHECK(views[0]->name() == std::string_view{"ram"});
    CHECK(views[0]->bytes().size() == 0x10000U);
    CHECK(views[1]->name() == std::string_view{"color_ram"});
    CHECK(views[1]->bytes().size() == 0x0400U);
}

TEST_CASE("c64_adapter ignores out-of-range input ports") {
    c64_adapter adapter = bare();
    mnemos::frontend_sdk::controller_state pad{};
    pad.a = true;
    adapter.apply_input(0, pad);
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored
    SUCCEED();
}

TEST_CASE("c64_adapter drain_audio resamples to 48 kHz output") {
    c64_adapter adapter = bare({.video_region = c64_config::region::ntsc});
    auto audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 48000U);

    adapter.step_one_frame();
    audio = adapter.drain_audio();
    CHECK(audio.sample_rate == 48000U);
    CHECK(audio.frame_count >= 798U);
    CHECK(audio.frame_count <= 802U);
    REQUIRE(audio.samples != nullptr);

    audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
}

TEST_CASE("c64_adapter with no media has no removable-media set") {
    c64_adapter adapter = bare();
    CHECK(adapter.kind() == c64_adapter::media_kind::none);
    CHECK(adapter.media_count() == 0U);
    CHECK_FALSE(adapter.insert_media(0));
}

TEST_CASE("c64_adapter wraps a bare PRG as a disk and mounts it") {
    c64_adapter adapter(basic_rom(), kernal_rom(), chargen_rom(), sample_prg(), {}, false);
    CHECK(adapter.kind() == c64_adapter::media_kind::disk);
    REQUIRE(adapter.media_count() == 1U);
    CHECK(adapter.system().drive8.mounted());
}

TEST_CASE("c64_adapter mounts a real D64 directly") {
    const auto disk = mnemos::chips::storage::c1541::make_prg_disk(sample_prg());
    REQUIRE(disk.size() == mnemos::chips::storage::c1541::d64_image::size_35_tracks);
    c64_adapter adapter(basic_rom(), kernal_rom(), chargen_rom(), disk, {}, false);
    CHECK(adapter.kind() == c64_adapter::media_kind::disk);
    CHECK(adapter.system().drive8.mounted());
}

TEST_CASE("c64_adapter loads a cartridge image") {
    // Minimal .crt: 64-byte header ("C64 CARTRIDGE   " + fields) and one CHIP
    // packet mapping 8 KiB at $8000 so EXROM/GAME assert and the cart inserts.
    std::vector<std::uint8_t> crt(0x40U, 0x00U);
    const char* magic = "C64 CARTRIDGE   ";
    for (std::size_t i = 0; i < 16U; ++i) {
        crt[i] = static_cast<std::uint8_t>(magic[i]);
    }
    crt[0x10] = 0x00;
    crt[0x11] = 0x00;
    crt[0x12] = 0x00;
    crt[0x13] = 0x40; // header length = 0x40
    crt[0x18] = 0x00; // EXROM asserted
    crt[0x19] = 0x00; // GAME asserted
    // CHIP packet.
    std::vector<std::uint8_t> chip(0x10U + 0x2000U, 0x00U);
    const char* chip_magic = "CHIP";
    for (std::size_t i = 0; i < 4U; ++i) {
        chip[i] = static_cast<std::uint8_t>(chip_magic[i]);
    }
    chip[0x06] = 0x00;
    chip[0x07] = 0x10 + 0x20; // total packet length 0x2010
    chip[0x0C] = 0x80;        // load address $8000
    chip[0x0E] = 0x20;        // image size 0x2000
    crt.insert(crt.end(), chip.begin(), chip.end());

    c64_adapter adapter(basic_rom(), kernal_rom(), chargen_rom(), crt, {}, true);
    CHECK(adapter.kind() == c64_adapter::media_kind::cartridge);
    CHECK(adapter.system().cart.inserted());
    CHECK(adapter.media_count() == 0U); // a cartridge is not removable disk media
}

TEST_CASE("c64_adapter supports a multi-disk set and swapping") {
    std::vector<std::vector<std::uint8_t>> extra;
    extra.push_back(mnemos::chips::storage::c1541::make_prg_disk(sample_prg()));
    extra.push_back(mnemos::chips::storage::c1541::make_prg_disk(sample_prg()));

    c64_adapter adapter(basic_rom(), kernal_rom(), chargen_rom(), sample_prg(), std::move(extra),
                        false);
    REQUIRE(adapter.media_count() == 3U);
    CHECK(adapter.current_media_index() == 0U);

    CHECK(adapter.insert_media(2));
    CHECK(adapter.current_media_index() == 2U);
    CHECK(adapter.system().drive8.mounted());

    CHECK_FALSE(adapter.insert_media(3)); // out of range
    CHECK(adapter.current_media_index() == 2U);
}

TEST_CASE("c64_adapter autostart types on the keyboard once warmed up") {
    // With autostart on and a disk mounted, after the warm-up delay the typist
    // drives the keyboard matrix. Sampling read_rows(0x00) (all columns driven)
    // shows a non-0xFF byte whenever a key is held. Zero ROMs never reach READY,
    // so the load itself can't complete here -- but the LOAD typing still runs.
    c64_adapter adapter(basic_rom(), kernal_rom(), chargen_rom(), sample_prg(), {}, true);
    bool saw_keypress = false;
    for (int i = 0; i < 240; ++i) {
        adapter.step_one_frame();
        if (adapter.system().input.read_rows(0x00U) != 0xFFU) {
            saw_keypress = true;
        }
    }
    CHECK(saw_keypress);
}

TEST_CASE("c64_adapter with autostart disabled never types") {
    c64_adapter adapter(basic_rom(), kernal_rom(), chargen_rom(), sample_prg(), {}, false);
    bool saw_keypress = false;
    for (int i = 0; i < 240; ++i) {
        adapter.step_one_frame();
        if (adapter.system().input.read_rows(0x00U) != 0xFFU) {
            saw_keypress = true;
        }
    }
    CHECK_FALSE(saw_keypress);
}

TEST_CASE("ascii_to_chord maps command characters to C64 keys") {
    CHECK(ascii_to_chord('L').count == 1U);
    CHECK(ascii_to_chord('L').keys[0] == key::l);
    CHECK(ascii_to_chord('8').keys[0] == key::k8);
    CHECK(ascii_to_chord('*').keys[0] == key::asterisk);
    CHECK(ascii_to_chord(',').keys[0] == key::comma);
    CHECK(ascii_to_chord('\r').keys[0] == key::ret);

    // A double-quote is SHIFT+'2'.
    const auto quote = ascii_to_chord('"');
    REQUIRE(quote.count == 2U);
    CHECK(quote.keys[0] == key::lshift);
    CHECK(quote.keys[1] == key::k2);

    // Lower case folds to the same key as upper case.
    CHECK(ascii_to_chord('r').keys[0] == key::r);
    // Unmapped characters yield an empty chord.
    CHECK(ascii_to_chord('#').count == 0U);
}
