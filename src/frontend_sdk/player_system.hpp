#pragma once

// The system-agnostic interface a windowed (or otherwise interactive) frontend
// uses to drive an emulated system. Concrete implementations live in the
// `manifests/<system>/` tiers (Genesis, SMS, C64, 32X, Sega CD, ...) so the
// same player binary can boot any of them without per-system glue in the
// frontend itself.
//
// The contract is intentionally small:
//
//   * one frame of pixels can be read at any time (the most recently
//     completed frame);
//   * the system advances exactly one video frame on demand (the frontend
//     paces these against vsync);
//   * input is a pre-mixed `controller_state` per port (the adapter maps
//     it onto its system's controller protocol);
//   * audio is drained as interleaved stereo s16 samples whose rate the
//     adapter declares (the frontend hands them to SDL_AudioStream which
//     resamples to the device rate).

#include "chip.hpp"                // for mnemos::chips::frame_buffer_view, ichip
#include "introspection_views.hpp" // for mnemos::instrumentation::memory_view
#include "peripheral.hpp"          // for mnemos::peripheral::controller_state
#include "save_state.hpp"          // for mnemos::runtime::load_result

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::frontend_sdk {

    // How the system's framebuffer is meant to face the player. Vertical
    // (TATE) arcade games render a portrait image on a rotated monitor; the
    // frontend rotates presentation 90 degrees clockwise. Consoles are
    // horizontal.
    enum class display_orientation : std::uint8_t { horizontal, vertical };

    // Video timing of the booted system. `frames_per_second_x1000` is the
    // refresh rate scaled by 1000 so NTSC (~59.94) and PAL (50.00) both fit
    // an integer without losing the meaningful decimal places.
    struct video_region final {
        std::uint32_t frames_per_second_x1000{60000U};
        display_orientation orientation{display_orientation::horizontal};
    };

    // System-agnostic controller state; the canonical definition lives in
    // peripheral/common so devices (tier 2) and frontends (tier 7) can both
    // reference it without a tier inversion. Re-exported here so existing
    // frontend_sdk callers don't have to change.
    using controller_state = mnemos::peripheral::controller_state;

    // A borrowed view of a stereo s16 audio chunk. `samples` is interleaved
    // L,R,L,R,... and points at `frame_count` (L,R) pairs at `sample_rate`
    // Hz. The pointer is valid until the next drain_audio() or
    // step_one_frame() on the same player_system, whichever comes first.
    struct audio_chunk final {
        const std::int16_t* samples{};
        std::uint32_t frame_count{};
        std::uint32_t sample_rate{};
    };

    // One labelled value the adapter publishes for the frontend's status
    // overlay. Both fields are opaque text -- each manifest picks the fields
    // that make sense for its hardware. Genesis publishes
    // System/Region/Cart; a future C64 adapter would publish
    // System/Region/Media/Title; an arcade-board adapter might publish
    // System/Board/Game. The frontend renders whatever it gets in order.
    struct spec_field final {
        std::string label;
        std::string value;
    };

    // Raw input-device shape published by an adapter. Discovery and frontends
    // derive policy from this; adapters only report what the loaded system
    // actually wires.
    enum class input_device_format : std::uint8_t {
        unknown,
        digital_pad,
        arcade_panel,
        keyboard,
        mouse,
        lightgun,
        analog
    };

    struct session_input_port final {
        std::uint32_t port_index{};
        std::uint8_t player_slot{};
        input_device_format format{input_device_format::unknown};
        std::string device_id{};
        std::string label{};
        bool connected{true};
    };

    struct session_capability_info final {
        std::vector<session_input_port> input_ports{};
        bool deterministic_frame_input{};
        bool save_state_supported{};
        bool frame_exact_save_state{};
        std::uint32_t max_input_delay_frames{};
    };

    enum class media_residency : std::uint8_t { resident, paged, streamed };

    enum class media_hash_algorithm : std::uint8_t { none, crc32, sha1, sha256 };

    struct media_page_hash final {
        std::uint64_t offset{};
        std::uint32_t byte_count{};
        std::string hash{};
    };

    struct media_image_info final {
        std::string id{};
        std::string label{};
        media_residency residency{media_residency::resident};
        std::uint64_t byte_count{};
        std::uint32_t page_size{};
        media_hash_algorithm hash_algorithm{media_hash_algorithm::none};
        std::string full_hash{};
        std::vector<media_page_hash> page_hashes{};
        std::string provider_id{};
        bool provider_available{true};
        std::string revision{};
        bool revision_supported{true};
        std::string cache_hint{};
    };

    struct media_capability_info final {
        std::vector<media_image_info> media{};
    };

    // A bootable, frame-steppable, presentable, input-consuming system. The
    // windowed player owns one of these and drives it; everything system-
    // specific lives behind this interface.
    class player_system {
      public:
        virtual ~player_system() = default;

        // Video timing for vsync pacing in the frontend.
        [[nodiscard]] virtual video_region region() const noexcept = 0;

        // Ordered set of name/value pairs the adapter wants visible in the
        // frontend's status overlay (system identity, region, loaded media,
        // ...). The adapter publishes this once after init (cart loaded or
        // empty-cart boot, same lifecycle either way) and the contents do
        // not change for the session, so callers pull a borrowed view of
        // the cached vector rather than rebuilding per call.
        [[nodiscard]] virtual const std::vector<spec_field>& system_spec() const noexcept = 0;

        // The most-recently-completed framebuffer. Pixel format and lifetime
        // follow chips::frame_buffer_view (0x00RRGGBB, valid until the next
        // frame-completing tick).
        [[nodiscard]] virtual chips::frame_buffer_view current_frame() const noexcept = 0;

        // Advance the system by exactly one video frame on its master clock.
        virtual void step_one_frame() = 0;

        // Latch `state` as the live input for `port` (0 = controller 1,
        // 1 = controller 2, ...). The adapter surfaces it on the next bus
        // read of the controller MMIO. Adapters MAY ignore ports past their
        // hardware's controller count.
        virtual void apply_input(int port, const controller_state& state) noexcept = 0;

        // Session/input metadata used by tooling to decide which multiplayer
        // modes are possible. Default empty keeps existing adapters honest:
        // unknown metadata is surfaced as unavailable/degraded capability data
        // rather than guessed from apply_input().
        [[nodiscard]] virtual const session_capability_info& session_capabilities() const noexcept {
            static const session_capability_info empty{};
            return empty;
        }

        // Descriptive media metadata for tooling. This does not imply a paging
        // implementation: adapters report resident, paged, or streamed media
        // facts here; bus reads and provider fetches remain system-specific.
        [[nodiscard]] virtual const media_capability_info& media_capabilities() const noexcept {
            static const media_capability_info empty{};
            return empty;
        }

        // Drain audio produced since the last drain (or since boot).
        // Returns an empty chunk (frame_count == 0) when the adapter has no
        // audio path wired yet -- the frontend handles silence cleanly.
        [[nodiscard]] virtual audio_chunk drain_audio() noexcept = 0;

        // Whole-session save/load. Adapters that publish
        // session_capability_info::save_state_supported must return a complete
        // runtime save-state container here and accept it through load_state().
        // Default empty/unsupported keeps older adapters honest until they wire
        // a full-machine target.
        [[nodiscard]] virtual std::vector<std::uint8_t> save_state() { return {}; }
        [[nodiscard]] virtual runtime::load_result load_state(std::span<const std::uint8_t> data) {
            (void)data;
            return {.status = runtime::load_status::unsupported_version};
        }

        // The chips that make up this system, returned in scheduler order so
        // debug consumers can present a stable view. Each pointer is non-owning
        // and valid for the lifetime of the player_system. Used by the
        // --screenshot path and any future debug UI to enumerate state via
        // `ichip::introspection()` without depending on a concrete adapter
        // type. Default empty -- adapters override to advertise their chips.
        [[nodiscard]] virtual std::span<chips::ichip* const> chips() const noexcept { return {}; }

        // System-level memories that aren't owned by any single chip -- the 68K /
        // Z80 work RAM, the I/O register file, etc. The --screenshot path dumps
        // these alongside each chip's introspection().memory_views() so external
        // game state (RAM counters, vsync flags) can be A/B'd byte-for-byte
        // against a reference. Default empty; adapters override to advertise theirs.
        [[nodiscard]] virtual std::span<instrumentation::memory_view* const>
        memory_views() const noexcept {
            return {};
        }

        // Optional, named runtime diagnostics requested by headless tooling before
        // artifact dump. Default unsupported keeps system-specific probes explicit.
        virtual bool run_debug_probe(std::string_view id) noexcept {
            (void)id;
            return false;
        }

        // Cartridge battery-backed RAM (SRAM), exposed so the frontend can persist
        // it to a .srm file across sessions: the frontend loads saved bytes into
        // this span on boot and writes it back on exit. The span is the adapter's
        // live backing store. Default empty -- only adapters with a battery-save
        // chip return non-empty (a cartridge console with no save RAM returns {}).
        [[nodiscard]] virtual std::span<std::uint8_t> battery_ram() noexcept { return {}; }

        // Convenience: lookup a chip by an adapter-stable id ("cpu", "vdp",
        // "z80", "sub_cpu", "vdp1", ...). Returns nullptr if the adapter
        // doesn't advertise that id. Default scans `chips()` for a match on
        // chip_metadata.part_number -- adapters can override for cheaper / id-
        // remapped lookup.
        [[nodiscard]] virtual chips::ichip* chip(std::string_view id) const noexcept {
            for (chips::ichip* c : chips()) {
                if (c != nullptr && c->metadata().part_number == id) {
                    return c;
                }
            }
            return nullptr;
        }

        // Removable-media control. Disk-based systems (a C64 with a multi-disk
        // game) expose a set of swappable images so the frontend can change the
        // medium in the drive when a program asks for the next disk -- the
        // emulated equivalent of the user swapping the floppy. Fixed-media
        // systems (cartridge consoles) leave the defaults: a count of 0 means
        // "no removable media", and the frontend hides any disk UI.
        [[nodiscard]] virtual std::size_t media_count() const noexcept { return 0; }
        [[nodiscard]] virtual std::size_t current_media_index() const noexcept { return 0; }
        // Swap the medium in the drive to `index`. Returns false (and changes
        // nothing) if the system has no removable media or `index` is out of
        // range. Takes effect immediately; the running program sees the new
        // disk on its next access, exactly as a physical swap would.
        virtual bool insert_media(std::size_t index) noexcept {
            (void)index;
            return false;
        }
    };

} // namespace mnemos::frontend_sdk
