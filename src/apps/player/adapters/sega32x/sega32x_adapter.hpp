#pragma once

// player_system adapter for the Sega 32X.
//
// Lives in the apps tier (8), alongside the Genesis / SMS / C64 / Sega CD
// adapters. Owns a sega32x_machine (a Genesis booting the 32X cartridge plus
// the Mars board's two SH-2s) and a scheduler wired against the Genesis chips
// (VDP /1, 68000 /7, Z80 /15, YM2612 /7, PSG /15 -- the VDP drives frame
// boundaries).
//
// The SH-2 pair runs at exactly 3x the 68000 clock, so step_one_frame()
// advances the Genesis a scanline of master cycles at a time and then catches
// the SH-2s up to 3x the 68000's progress -- fine enough that the COMM-register
// boot handshake makes progress every poll.
//
// Video is composition: the Genesis VDP renders its frame as usual; at the
// frame boundary the 32X VDP overlays its frame-buffer pixels (packed /
// direct / run-length, with per-pixel priority) onto a copy the adapter owns.
// Per-scanline composition (mid-frame palette/bank changes) is a later
// refinement.
//
// Audio mixes the Genesis FM + PSG with the 32X PWM pair.

#include "introspection_views.hpp" // span_memory_view
#include "player_system.hpp"
#include "region.hpp"
#include "scheduler.hpp"
#include "scheduler_factory.hpp"
#include "sega32x_machine.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::sega32x {

    // Force-link hook so the adapter_registry self-registration survives the
    // linker (main.cpp calls it once at startup).
    void force_link() noexcept;

    class sega32x_adapter final : public frontend_sdk::player_system {
      public:
        // Build around a moved-in 32X cartridge image plus the three boot ROMs
        // (any may be empty -- the machine then boots the cart vectors directly,
        // which is what the BIOS-less unit tests rely on).
        explicit sega32x_adapter(std::vector<std::uint8_t> cart,
                                 manifests::sega32x::sega32x_bios bios = {},
                                 const manifests::genesis::genesis_config& config = {},
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
        [[nodiscard]] std::span<instrumentation::memory_view* const>
        memory_views() const noexcept override {
            return system_mem_view_;
        }
        // 32X carts use the same Genesis-side battery stores (header SRAM or
        // serial EEPROM); the frontend persists them to a .srm across sessions.
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
            if (machine_->genesis->eeprom.device) {
                return machine_->genesis->eeprom.device->bytes();
            }
            return machine_->genesis->sram.data;
        }

        // For tests / instrumentation.
        [[nodiscard]] std::uint64_t frames_stepped() const noexcept { return frames_stepped_; }
        [[nodiscard]] manifests::sega32x::sega32x_machine& machine() noexcept { return *machine_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        // Overlay the 32X frame onto the freshly completed Genesis frame.
        void compose_frame() noexcept;

        std::unique_ptr<manifests::sega32x::sega32x_machine> machine_;
        instrumentation::span_memory_view work_ram_view_;
        instrumentation::span_memory_view sdram_view_;
        instrumentation::span_memory_view fb_view_;
        std::array<instrumentation::memory_view*, 3> system_mem_view_{};
        std::array<chips::ichip*, 8> chip_view_{};
        runtime::scheduler scheduler_;
        std::array<frontend_sdk::controller_state, 2> ports_{};
        mnemos::video_region region_;
        double target_fps_;
        std::uint64_t frames_stepped_{};
        std::vector<frontend_sdk::spec_field> spec_{};

        // The composed output frame (a copy of the Genesis frame with the 32X
        // overlay applied); geometry mirrors the Genesis framebuffer view.
        std::vector<std::uint32_t> composed_{};
        chips::frame_buffer_view composed_view_{};

        // Audio mix scratch: Genesis FM (stereo) + PSG (mono) + 32X PWM
        // (stereo), each resampled to the output rate and summed into the
        // int32 accumulators before clipping into mix_buf_.
        std::vector<std::int16_t> fm_buf_{};
        std::vector<std::int16_t> psg_buf_{};
        std::vector<std::int16_t> pwm_buf_{};
        std::vector<std::int32_t> acc_l_{};
        std::vector<std::int32_t> acc_r_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
    };

} // namespace mnemos::apps::player::adapters::sega32x
