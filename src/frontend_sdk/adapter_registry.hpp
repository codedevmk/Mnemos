#pragma once

// Process-wide registry of adapter factories keyed by system family name
// ("genesis", "sms", "c64", "32x", ...). Each adapter source file populates
// the registry at static-init time so the player binary can boot a system
// from a string ID without compile-time knowledge of which adapter types
// exist. Tier 7 (frontend_sdk) -- shared by the player executable and by
// any tool that wants to instantiate systems generically.
//
// LINKER NOTE: an adapter library that nothing else references can be
// discarded entirely by the linker, which silently disables its static-init
// self-registration. To prevent this, each adapter exposes a `force_link()`
// no-op function declared in its header; the player core calls those once
// before construction. Adding a new adapter is: drop the adapter directory +
// add one force_link() call in the player adapter-retention unit. The executable
// entrypoint never names a concrete adapter type.

#include "player_system.hpp"
#include "region.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mnemos::frontend_sdk {

    class scheduler_factory; // scheduler_factory.hpp

    struct msx_slot_location final {
        std::uint8_t primary{};
        std::uint8_t secondary{};
    };

    struct msx_machine_profile final {
        std::optional<std::uint8_t> expanded_primary_slots{};
        std::optional<msx_slot_location> ram_slot{};
        std::optional<msx_slot_location> sub_bios_slot{};
        std::optional<msx_slot_location> disk_slot{};
        std::optional<msx_slot_location> cartridge2_slot{};
        std::optional<std::size_t> ram_size{};
    };

    // Inputs every adapter factory takes. Future adapters may need richer
    // config (CD-ROM image, BIOS path, manifest selection); extend this
    // struct rather than the factory signature so existing factories keep
    // working.
    struct adapter_options final {
        std::vector<std::uint8_t> rom;
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        std::string display_name;
        // Additional media images beyond the primary `rom`, in load order. Used
        // by disk-based systems for multi-disk games: the C64 adapter mounts
        // `rom` as disk 1 and these as disks 2..N, swappable at runtime via
        // player_system::insert_media. Empty for single-image media. Each entry
        // is interpreted the same way the adapter interprets `rom`.
        std::vector<std::vector<std::uint8_t>> additional_media;
        // Filesystem paths corresponding to `additional_media` entries. Empty
        // paths are allowed for synthetic media; file-backed arcade adapters use
        // these to validate supplemental ROM-set fragments against the selected
        // manifest before merging them.
        std::vector<std::string> additional_media_paths;
        // Whether the adapter should auto-start the loaded media (a disk/tape
        // computer types LOAD/RUN for you). Adapters without an autostart
        // concept ignore it. Defaults on; the frontend clears it to drop to a
        // bare prompt instead.
        bool autostart{true};
        // Optional DIP-switch override for arcade families (the raw bank
        // bits the board's DSW ports read); nullopt = the adapter's factory
        // defaults. Console families ignore it.
        std::optional<std::uint16_t> dip_override{};
        // Optional cartridge-mapper override, interpreted by the family adapter
        // (e.g. the SMS adapter accepts "sega" / "codemasters" / "korean").
        // Empty = let the adapter auto-detect. Families without selectable
        // mappers ignore it.
        std::string mapper_override;
        // Optional second cartridge/media slot mapper override. Empty = family
        // default. Families without a second mapper-selectable slot ignore it.
        std::string mapper2_override;
        // Optional FM expansion. SMS uses this for the Japanese Master System /
        // FM Sound Unit YM2413; MSX uses it for MSX-MUSIC/FM-PAC OPLL plus PAC
        // SRAM. Families without an FM expansion ignore it.
        bool fm_unit{};
        // Plug a light gun into the family's gun port (NES Zapper on port 2, ...).
        // Families without a light gun ignore it.
        bool light_gun{};
        // Plug a 4-player multitap into the family's ports (NES Four Score, ...).
        // Families without a multitap ignore it.
        bool four_score{};
        // Optional battery-backed real-time clock. MSX uses this for RP-5C01
        // clock/CMOS hardware; families without an RTC ignore it.
        bool rtc{};
        // Select MSX2-class video hardware where the family supports it. MSX
        // maps this to the V9938; other families ignore it.
        bool msx2{};
        // Optional MSX/MSX2 machine profile, used by real machine proofs and
        // launches whose firmware expects a specific slot/subslot layout.
        msx_machine_profile msx_profile{};
        // Filesystem path of the primary CD/disk image, for media that loads by
        // path rather than a flat byte buffer (a .cue references sibling .bin
        // tracks; an .iso is read whole -- .chd is not supported yet). The Sega CD
        // adapter opens this with disc_image::open while `rom` carries the boot
        // BIOS. Empty for byte-buffer media.
        std::string disc_path;
        // Filesystem path of the primary `rom` itself (when it came from a file).
        // Lets an adapter resolve sibling files beside it -- e.g. a CPS1 clone
        // set whose game.toml names a `parent` set loads the parent's shared
        // dumps from `<dir>/<parent>.zip`. Empty when the rom is not file-backed.
        std::string rom_path;
        // System boot ROM images beyond the primary `rom`, in an adapter-defined
        // order (the 32X adapter takes master SH-2 / slave SH-2 / 68000 vector
        // overlay). Empty entries (or an empty vector) mean "boot without that
        // image"; the adapter decides whether that is viable.
        std::vector<std::vector<std::uint8_t>> bios_images;
        // Optional physical keyboard layout token. Computer adapters can use
        // this to map host HID usages to the target machine's raw key matrix.
        // Empty = adapter default. Console/arcade families ignore it.
        std::string keyboard_layout_override;
        // Optional scheduler-construction override. null = adapter falls back
        // to its built-in scheduler. Non-null lets tooling (deterministic
        // replay, profilers, slice-based multi-clock for 32X/Saturn/CD)
        // intercept scheduler construction without modifying adapter code.
        // Non-owning; caller keeps the factory alive for the adapter's
        // construction call.
        scheduler_factory* scheduler_factory_override{};
    };

    class adapter_registry final {
      public:
        using factory = std::function<std::unique_ptr<player_system>(adapter_options options)>;

        adapter_registry(const adapter_registry&) = delete;
        adapter_registry& operator=(const adapter_registry&) = delete;
        adapter_registry(adapter_registry&&) = delete;
        adapter_registry& operator=(adapter_registry&&) = delete;

        // The single, process-wide registry. Constructed lazily on first
        // access (no global-ordering issues for static-init self-registers).
        [[nodiscard]] static adapter_registry& instance();

        // Map a family ID ("genesis", "sms", ...) to a factory. Re-registering
        // the same family replaces the previous factory; not an error -- the
        // expected use is each adapter source registers its own family once
        // at static-init, and re-registration only happens in tests.
        void register_family(std::string family, factory fn);

        // Invoke the factory for `family`. Returns nullptr (without
        // constructing a system) if no adapter is registered under that name.
        [[nodiscard]] std::unique_ptr<player_system> create(std::string_view family,
                                                            adapter_options options) const;

        // Snapshot of currently-registered family IDs. Useful for diagnostic
        // listings ("supported systems: genesis, sms, ...").
        [[nodiscard]] std::vector<std::string> families() const;

      private:
        adapter_registry() = default;

        mutable std::mutex mu_{};
        std::unordered_map<std::string, factory> factories_{};
    };

} // namespace mnemos::frontend_sdk
