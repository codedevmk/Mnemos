#pragma once

// player_system adapter for the Sega CD / Mega CD.
//
// Lives in the apps tier (8), alongside the Genesis / SMS / C64 adapters. Owns a
// segacd_machine (a Genesis booting the Sega CD BIOS + the CD sub side) and a
// scheduler wired against the Genesis chips (VDP /1, 68000 /7, Z80 /15,
// YM2612 /7, PSG /15 -- the VDP drives frame boundaries).
//
// The sub-CPU (12.5 MHz) and the CD drive (75 Hz frames) are NOT clean dividers
// of the 53.69 MHz Genesis master clock, so step_one_frame() advances the
// Genesis a frame on the scheduler and then drives the sub-CPU + CD frames by a
// per-frame accumulator (fractional remainders carried so the long-term rate is
// exact). The sub-CPU only runs once the BIOS releases it (gate $01).
//
// Audio mixes the Genesis FM + PSG with the Sega CD CD-DA + RF5C164 PCM.

#include "disc_image.hpp"          // mnemos::disc::disc_image (the mounted CD)
#include "introspection_views.hpp" // span_memory_view
#include "player_system.hpp"
#include "region.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"
#include "segacd_machine.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::segacd {

    // Force-link hook so the adapter_registry self-registration survives the
    // linker (main.cpp calls it once at startup).
    void force_link() noexcept;

    class segacd_adapter final : public frontend_sdk::player_system {
      public:
        // Build around a moved-in BIOS image (the Genesis main CPU boots it as its
        // cartridge; the sub-CPU boots from PRG-RAM vectors the main BIOS loads
        // there). A disc is attached separately via machine().sub->attach_disc().
        explicit segacd_adapter(std::vector<std::uint8_t> bios,
                                const manifests::genesis::genesis_config& config = {},
                                std::string display_name = {},
                                frontend_sdk::scheduler_factory* scheduler_factory = nullptr,
                                const std::string& disc_path = {});

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
        [[nodiscard]] std::span<instrumentation::memory_view* const>
        memory_views() const noexcept override {
            return system_mem_view_;
        }
        // The Sega CD's internal 8 KiB backup RAM is the battery-backed save
        // store; the frontend persists it to a .srm/.brm across sessions.
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
            return machine_->sub->backup_ram;
        }

        // For tests / instrumentation.
        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::segacd::segacd_machine& machine() noexcept { return *machine_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::segacd::segacd_machine> machine_;
        // The mounted CD image (borrowed by machine_->sub via attach_disc); null
        // when booting the BIOS with no disc.
        std::unique_ptr<mnemos::disc::disc_image> disc_;
        instrumentation::span_memory_view work_ram_view_;
        instrumentation::span_memory_view prg_ram_view_;
        std::array<instrumentation::memory_view*, 2> system_mem_view_{};
        std::array<chips::ichip*, 5> chip_view_{};
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        mnemos::video_region region_;
        double target_fps_;
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};

        // Audio mix scratch: Genesis FM (stereo) + PSG (mono) + sub RF5C164 PCM
        // (stereo) + CD-DA (stereo), each resampled to the output rate and summed
        // into the int32 accumulators before clipping into mix_buf_.
        std::vector<std::int16_t> fm_buf_{};
        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> pcm_buf_{};
        std::vector<std::int16_t> cdda_buf_{};
        std::vector<std::int32_t> acc_l_{};
        std::vector<std::int32_t> acc_r_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};

        // Sub-CPU + CD-frame pacing. Both run off their own clocks (12.5 MHz /
        // 75 Hz), not Genesis master dividers, so they advance per video frame at
        // rate/fps with the fractional remainder carried for an exact long-term
        // rate.
        double sub_cycle_frac_{0.0};
        double cd_frame_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::segacd
