#pragma once

// player_system adapter for the Commodore 64.
//
// Lives in the apps tier (8) and bridges the C64 manifest (tier 4) to the
// frontend_sdk::player_system interface (tier 7) -- the same role the Genesis
// and SMS adapters play for their systems. The player tool depends on whichever
// adapters a build wants supported; this one self-registers under "c64".
//
// The C64 is a home computer, not a cartridge console, so the bridge it builds
// differs from the console adapters even though the outward player_system
// contract is identical -- which is exactly what the multi-system architecture
// is for:
//
//   * Boot ROMs vs cart: there is no game cartridge in the common case. The
//     machine boots from three system images (BASIC 8K, KERNAL 8K, CHARGEN 4K).
//     The factory sources them from $MNEMOS_C64_ROM_DIR (matching the manifest
//     parity test), falling back to correctly-sized zero images so the player
//     still launches without a ROM set (boots to a blank machine).
//   * Media is loaded the way the real device would, not injected: a disk is
//     mounted on the synthetic 1541 and the KERNAL pulls it over IEC; a tape is
//     inserted and played so its pulses feed CIA1 /FLAG (the loading colour
//     stripes then emerge from the genuine KERNAL writing the border register);
//     a cartridge maps itself and boots; a bare .prg is wrapped into an
//     in-memory .d64 so it loads over IEC too.
//   * Autostart types LOAD/RUN on the keyboard matrix the way a user would,
//     after the KERNAL has reached its prompt -- no HLE trap.
//   * Multi-disk games swap images in the drive via insert_media().
//   * Audio: the SID is sampled per phi2 cycle through its opt-in capture queue,
//     then downsampled to 48 kHz -- the same shape as the SMS PSG path.
//   * Input: controller_state maps onto a digital joystick on a C64 control
//     port (player 0 -> port 2, the usual game port).
//
// One step_one_frame() advances exactly one VIC-II video frame.

#include "c64_input.hpp"   // c64_input::key (autostart key translation)
#include "c64_runtime.hpp" // manifest-path build (also pulls c64_config)
#include "player_system.hpp"
#include "region.hpp" // chips/shared: video_region
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::c64 {

    // Force-link hook so the adapter_registry self-registration in
    // c64_adapter.cpp survives the linker (see genesis_adapter.hpp for the
    // rationale). main.cpp calls this once at startup.
    void force_link() noexcept;

    // One keystroke the autostart typist presses together: usually a single key,
    // but a chord (SHIFT + key) for characters like the double-quote. Exposed
    // for testing the ASCII -> key translation.
    struct key_chord final {
        std::array<manifests::c64::c64_input::key, 2> keys{};
        std::uint8_t count{};
    };

    // The C64 key chord that produces ASCII character `c` at the unshifted
    // BASIC prompt. Letters fold to their (case-insensitive) key, digits to the
    // number row, '"' to SHIFT+'2', and '*'/',' to their own keys. Returns an
    // empty chord (count == 0) for characters the typist doesn't need.
    [[nodiscard]] key_chord ascii_to_chord(char c) noexcept;

    class c64_adapter final : public frontend_sdk::player_system {
      public:
        // How the primary media image is interpreted, sniffed from its contents.
        enum class media_kind : std::uint8_t { none, disk, tape, cartridge };

        // Build an adapter around the three moved-in system ROM images plus an
        // optional primary media image and any extra disks (for multi-disk
        // games). `media` is sniffed (CRT / TAP / D64 / else .prg, which is
        // wrapped into a disk); `additional_disks` are extra images mounted on
        // demand via insert_media. `autostart` types LOAD/RUN once the KERNAL is
        // up. `config.video_region` selects 50 Hz PAL vs 60 Hz NTSC pacing.
        // `display_name` is the status-overlay label (empty = no media row).
        explicit c64_adapter(std::vector<std::uint8_t> basic_rom,
                             std::vector<std::uint8_t> kernal_rom,
                             std::vector<std::uint8_t> chargen_rom,
                             std::vector<std::uint8_t> media = {},
                             std::vector<std::vector<std::uint8_t>> additional_disks = {},
                             bool autostart = true, const manifests::c64::c64_config& config = {},
                             std::string display_name = {},
                             frontend_sdk::scheduler_factory* scheduler_factory = nullptr);

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override;
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return chip_view_;
        }

        // Removable media: the mounted disk set (empty for tape/cart/bare boot).
        [[nodiscard]] std::size_t media_count() const noexcept override { return disks_.size(); }
        [[nodiscard]] std::size_t current_media_index() const noexcept override {
            return disk_index_;
        }
        bool insert_media(std::size_t index) noexcept override;

        // For tests / instrumentation.
        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] media_kind kind() const noexcept { return kind_; }
        [[nodiscard]] manifests::c64::c64_runtime& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        void tick_autostart();

        std::unique_ptr<manifests::c64::c64_runtime> sys_;
        // Non-owning chip pointers in scheduler order (VIC, CPU, CIA1, CIA2,
        // SID). Exposed via player_system::chips() so generic debug tooling can
        // enumerate them without depending on c64_runtime.
        std::array<chips::ichip*, 5> chip_view_{};
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        mnemos::video_region region_;
        double target_fps_;
        std::uint64_t frames_stepped_{};

        media_kind kind_{media_kind::none};
        // Mounted disk set (D64 images). disks_[disk_index_] is in the drive.
        std::vector<std::vector<std::uint8_t>> disks_{};
        std::size_t disk_index_{};

        std::vector<frontend_sdk::spec_field> spec_{};

        // Autostart typist: a queue of chords pressed one at a time, each held
        // then released over a few frames so the KERNAL keyscan registers it.
        // Sequencing is a small state machine ticked once per frame.
        enum class auto_phase : std::uint8_t {
            idle,
            warmup,
            type_load,
            await_drive,
            type_run,
            done
        };
        auto_phase phase_{auto_phase::idle};
        std::vector<key_chord> queue_{};
        std::size_t queue_pos_{};
        std::uint32_t phase_frames_{};
        std::uint32_t hold_frames_{};
        bool drive_was_active_{};

        // Audio scratch (see drain_audio).
        std::vector<std::int16_t> sid_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::c64
