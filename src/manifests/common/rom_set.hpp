#pragma once

// Arcade ROM-set assembly. A console loads one cart image; an arcade game is
// a SET of dump files composed into named regions -- an even/odd interleaved
// pair for a 16-bit CPU's program, separate tile / sprite / sample regions --
// each file individually CRC-verifiable. The declarations here are populated
// by a board/game manifest; the loader is pure logic over an injected file
// provider, so a set loads identically from a directory of loose dumps or a
// .zip bundle (providers for both below).
//
// Verification failures (missing file, size mismatch, CRC mismatch, region
// overflow) are collected as issues rather than aborting the load: whatever
// data is present is still placed, the caller decides via ok() whether to
// boot. This mirrors how arcade emulators surface bad dumps without refusing
// to start development work.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::manifests::common {

    // One dump file's placement within a region. The source span (`length`
    // bytes from `source_offset`, defaulting to the whole file) is copied in
    // `unit`-byte chunks; each chunk lands at `offset` and advances the
    // destination by `stride`. So the defaults (unit 1, stride 1) are a plain
    // contiguous copy; unit 1 / stride 2 is the even/odd pairing of a 16-bit
    // CPU's program ROM pair; unit 2 / stride 8 is a 16-bit graphics ROM
    // dropped into one lane of a 64-bit tile word; `source_offset` + `length`
    // place a slice of a larger dump. `swap` reverses the byte order within
    // each unit -- a word-swapped 16-bit ROM whose endianness is flipped
    // relative to the region.
    struct rom_set_file final {
        std::string name;
        std::size_t offset{};                 // first destination byte
        std::size_t stride{1U};               // destination step per source unit
        std::size_t unit{1U};                 // source bytes per chunk (contiguous)
        bool swap{};                          // reverse byte order within each unit
        std::size_t source_offset{};          // first source byte to read
        std::size_t length{};                 // source bytes to place; 0 = rest of file
        std::size_t size{};                   // expected file byte count; 0 = accept any
        std::optional<std::uint32_t> crc32{}; // verified when set
    };

    struct rom_set_region final {
        std::string name; // "maincpu", "gfx_tiles", "samples", ...
        std::size_t size{};
        std::uint8_t fill{0xFFU}; // value of bytes no file covers
        std::vector<rom_set_file> files;
    };

    // Monitor orientation the board is wired for. A vertical (TATE) set is
    // rotated by the frontend for upright presentation; absent in the TOML =>
    // horizontal.
    enum class screen_orientation : std::uint8_t { horizontal, vertical };

    struct rom_set_decl final {
        std::string name;  // set id, e.g. "rtype"
        std::string board; // board family id, e.g. "irem_m72" (informational)
        // Optional CPS-B board / PAL profile id: capcom_cps1 boards select their
        // hardware profile by this numeric id; absent on families that don't use it.
        std::optional<std::uint16_t> cps_b_profile;
        // Display orientation (default horizontal); the frontend rotates a
        // vertical set's framebuffer for upright presentation.
        screen_orientation orientation{screen_orientation::horizontal};
        // Optional sound subsystem selector (board-interpreted): capcom_cps1 reads
        // "qsound" to wire its QSound DSP path instead of the OKIM6295 path; absent
        // => the board default.
        std::optional<std::string> sound;
        // Optional Kabuki-encrypted-sound key name (board-interpreted): capcom_cps1
        // reads "dino" / "wof" / "punisher" to decrypt the QSound Z80 program.
        std::optional<std::string> kabuki;
        std::vector<rom_set_region> regions;
    };

    // Resolves a dump-file name to its bytes; nullopt when the set lacks it.
    using rom_file_provider =
        std::function<std::optional<std::vector<std::uint8_t>>(std::string_view name)>;

    struct rom_load_issue final {
        std::string file; // offending file (or region) name
        std::string message;
    };

    struct rom_set_image final {
        std::map<std::string, std::vector<std::uint8_t>, std::less<>> regions;
        std::vector<rom_load_issue> issues;

        [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
        [[nodiscard]] const std::vector<std::uint8_t>*
        region(std::string_view name) const noexcept {
            const auto it = regions.find(name);
            return it != regions.end() ? &it->second : nullptr;
        }
    };

    [[nodiscard]] rom_set_image load_rom_set(const rom_set_decl& decl,
                                             const rom_file_provider& provider);

    // Provider over a directory of loose dump files (name -> directory/name).
    [[nodiscard]] rom_file_provider make_directory_rom_provider(std::string directory);

    // Provider over an in-memory .zip bundle (entry names are the file names).
    // The provider owns the archive bytes. nullopt when `archive` is not a
    // readable zip.
    [[nodiscard]] std::optional<rom_file_provider>
    make_zip_rom_provider(std::vector<std::uint8_t> archive);

} // namespace mnemos::manifests::common
