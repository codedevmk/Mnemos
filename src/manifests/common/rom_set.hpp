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
        // Alternate dump names accepted for the same bytes. This keeps
        // split-parent zips and standalone archives loadable when they carry
        // CRC-identical dumps under board-location-specific labels.
        std::vector<std::string> aliases;
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

    // Sprite-list draw order the board renders in (board-interpreted; capcom_cps1
    // maps it to its video chip). Some bootleg sets relocate the object list and
    // must be drawn in reverse; absent in the TOML => ascending (the hardware
    // default for every official set).
    enum class sprite_draw_order : std::uint8_t { ascending, descending };

    struct rom_set_dip_option final {
        std::string label;
        // Full 16-bit DSW word bits covered by the parent switch mask. M72 reads
        // SW1 in the low byte and SW2 in the high byte.
        std::uint16_t value{};
    };

    struct rom_set_dip_condition final {
        // The option group is active when `(dip_word & mask) == value`.
        std::uint16_t mask{};
        std::uint16_t value{};
    };

    struct rom_set_dip_switch final {
        std::string bank; // display grouping, e.g. "SW1" / "SW2"
        std::string name;
        std::uint16_t mask{};
        std::uint16_t default_value{};
        std::optional<rom_set_dip_condition> condition;
        std::vector<rom_set_dip_option> options;
    };

    // Explicit high-level emulation substitution. The constitution requires every
    // non-cycle-accurate/HLE chip path to be visible in the manifest rather than
    // hidden in a board implementation. `profile` is optional (empty when the
    // substitution needs no profile id, e.g. CPS2 sets).
    struct rom_set_hle_sample_trigger final {
        std::uint8_t trigger{};
        std::uint32_t start{};
    };

    struct rom_set_hle_decl final {
        std::string chip;
        std::string profile;
        std::string rationale;
        // Optional board-interpreted sample cursor declarations carried by the
        // same explicit HLE profile that consumes them.
        std::vector<rom_set_hle_sample_trigger> sample_triggers;
    };

    struct rom_set_decl final {
        std::string name;  // set id, e.g. "rtype"
        std::string board; // board family id, e.g. "irem_m72" (informational)
        // Optional parent set name (MAME-style clone -> parent). A clone set's
        // zip ships only its unique dumps; the shared dumps come from the parent
        // set's zip. The board adapter composes a fallback provider (the clone's
        // own files first, then the parent's) -- every file is still CRC-verified
        // regardless of which zip supplied it. Absent => a standalone set.
        // Constrained to a plain set id by the loader (no path separators).
        // NOTE: capcom_cps1 and taito_f2 consume this by threading
        // adapter_options.rom_path and composing the fallback. Other boards parse
        // it but ignore it, so a `parent` there would report the shared files
        // missing. Single level only -- the parent set must be standalone, not
        // itself a clone.
        std::optional<std::string> parent;
        // Optional CPS-B board / PAL profile id: capcom_cps1 boards select their
        // hardware profile by this numeric id; absent on families that don't use it.
        std::optional<std::uint16_t> cps_b_profile;
        // Display orientation (default horizontal); the frontend rotates a
        // vertical set's framebuffer for upright presentation.
        screen_orientation orientation{screen_orientation::horizontal};
        // Local player panel count exposed by the board/cabinet. Arcade boards
        // default to two panels unless a set declares a dedicated 3P/4P layout.
        std::uint8_t players{2U};
        // Optional board-interpreted input/cabinet wiring profile. CPS2 uses this
        // to distinguish six-button fighters from cabinets that repurpose IN1.
        std::optional<std::string> input;
        // Sprite-list draw order (default ascending); a few bootleg sets relocate
        // the object list and declare "descending". Board-interpreted (capcom_cps1).
        sprite_draw_order sprite_order{sprite_draw_order::ascending};
        // Optional sound subsystem selector (board-interpreted): capcom_cps1 reads
        // "qsound" to wire its QSound DSP path instead of the OKIM6295 path; absent
        // => the board default.
        std::optional<std::string> sound;
        // Optional Kabuki-encrypted-sound key name (board-interpreted): capcom_cps1
        // reads "dino" / "wof" / "punisher" to decrypt the QSound Z80 program.
        std::optional<std::string> kabuki;
        // Optional cabinet DIP switch metadata. Values are the board's raw
        // active-low bit pattern after applying `mask`; adapters decide how to
        // surface or edit them.
        std::vector<rom_set_dip_switch> dips;
        // Explicit high-level substitution declarations. These are never a
        // hidden fallback: a board may consume a known profile only when the
        // manifest declares the substituted chip and rationale.
        std::vector<rom_set_hle_decl> hle;
        // Optional Taito F2 board wiring selectors. The family is not one fixed
        // decode: games move the IO/sound/video windows and several need distinct
        // sprite buffering or banking behavior.
        std::optional<std::string> taito_f2_map;
        std::optional<std::string> taito_f2_sprite_policy;
        std::optional<std::string> taito_f2_sprite_buffering;
        std::optional<std::string> taito_f2_palette_format;
        std::optional<std::uint32_t> taito_f2_sprite_extension_base;
        std::optional<std::uint32_t> taito_f2_sprite_extension_size;
        std::optional<std::string> taito_f2_sprite_active_area;
        std::optional<std::int16_t> taito_f2_sprite_hide_pixels;
        std::optional<std::int16_t> taito_f2_sprite_flip_hide_pixels;
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

    // Builds an effective clone declaration from a standalone parent and a
    // child clone declaration. Child metadata wins when present; regions are
    // inherited by name, with a child region replacing the parent's region of
    // the same name.
    [[nodiscard]] rom_set_decl inherit_parent_regions(const rom_set_decl& parent,
                                                      rom_set_decl child);

    // Provider over a directory of loose dump files (name -> directory/name).
    [[nodiscard]] rom_file_provider make_directory_rom_provider(std::string directory);

    // Provider over an in-memory .zip bundle (entry names are the file names).
    // The provider owns the archive bytes. nullopt when `archive` is not a
    // readable zip.
    [[nodiscard]] std::optional<rom_file_provider>
    make_zip_rom_provider(std::vector<std::uint8_t> archive);

    // Open a .zip set from a filesystem path (read_file + make_zip_rom_provider).
    // nullopt when the path is unreadable; `*unreadable_zip` (when provided) is
    // set true if the file was read but is not a valid zip, so a caller can tell
    // "missing" from "corrupt".
    [[nodiscard]] std::optional<rom_file_provider>
    make_zip_rom_provider_from_path(const std::string& path, bool* unreadable_zip = nullptr);

    // Compose two providers into one: a name is resolved from `primary` first
    // and, only if `primary` lacks it, from `fallback`. This is the mechanism
    // behind MAME-style clone/parent merging -- the clone's zip is `primary`
    // (its unique dumps win), the parent's zip is `fallback` (the shared ones).
    // A null sub-provider contributes nothing.
    [[nodiscard]] rom_file_provider make_fallback_rom_provider(rom_file_provider primary,
                                                               rom_file_provider fallback);

} // namespace mnemos::manifests::common
