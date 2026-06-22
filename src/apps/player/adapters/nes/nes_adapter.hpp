#pragma once

#include "nes_system.hpp"
#include "player_system.hpp"
#include "save_state.hpp" // runtime::save_target
#include "scheduler.hpp"
#include "scheduler_factory.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters::nes {

    // Force-link hook (see genesis_adapter.hpp): referenced from main so the
    // static registry self-registration in the .cpp is pulled in.
    void force_link() noexcept;

    // Build a whole-machine save target for a hand-wired NES: the three chips
    // (their own save_state), the work + cartridge RAM, and the mapper banking
    // state (a save_component, since the mapper is not an ichip). CHR is included
    // only when it is RAM. The runtime serialises/restores each chunk by id.
    [[nodiscard]] runtime::save_target build_save_target(manifests::nes::nes_system& sys);

    // NES player adapter. Drives a hand-wired nes_system through the runtime
    // scheduler. The 2C02 PPU is the frame source and the master clock; the CPU
    // and APU run at a third of its dot rate (NTSC 3:1). inc 1 scope: boots an
    // NROM cart + renders. Audio (APU) and controllers are follow-ups.
    class nes_adapter final : public frontend_sdk::player_system {
      public:
        explicit nes_adapter(std::vector<std::uint8_t> rom,
                             const manifests::nes::nes_config& config = {},
                             std::string display_name = {},
                             frontend_sdk::scheduler_factory* scheduler_factory = nullptr);

        [[nodiscard]] frontend_sdk::video_region region() const noexcept override;
        [[nodiscard]] const std::vector<frontend_sdk::spec_field>&
        system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] chips::frame_buffer_view current_frame() const noexcept override {
            return sys_->ppu.framebuffer();
        }
        // Persist a cart's save medium. A mapper that carries its own non-volatile
        // store (the Bandai FCG's serial EEPROM) wins; otherwise the $6000-$7FFF RAM
        // persists only for battery-backed carts (Zelda, Final Fantasy, ...), and
        // plain work-RAM carts return empty so no .srm is written.
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
            if (sys_->mapper) {
                const std::span<std::uint8_t> medium = sys_->mapper->battery_ram();
                if (!medium.empty()) {
                    return medium;
                }
            }
            return sys_->battery ? std::span<std::uint8_t>(sys_->prg_ram)
                                 : std::span<std::uint8_t>{};
        }
        void step_one_frame() override;
        void apply_input(int port, const frontend_sdk::controller_state& state) noexcept override;
        [[nodiscard]] const frontend_sdk::session_capability_info&
        session_capabilities() const noexcept override {
            return session_;
        }
        [[nodiscard]] frontend_sdk::audio_chunk drain_audio() noexcept override;
        [[nodiscard]] std::span<chips::ichip* const> chips() const noexcept override {
            return {chip_view_.data(), chip_view_.size()};
        }

        // FDS multi-side disk swapping: the disk image's sides are the swappable
        // media the player's F6 flips between. A cartridge reports none.
        [[nodiscard]] std::size_t media_count() const noexcept override {
            return sys_->mapper ? sys_->mapper->disk_side_count() : 0U;
        }
        [[nodiscard]] std::size_t current_media_index() const noexcept override {
            return sys_->mapper ? sys_->mapper->current_disk_side() : 0U;
        }
        bool insert_media(std::size_t index) noexcept override {
            if (sys_->mapper == nullptr || index >= sys_->mapper->disk_side_count()) {
                return false;
            }
            sys_->mapper->insert_disk_side(index);
            return true;
        }

        [[nodiscard]] manifests::nes::nes_system& system() noexcept { return *sys_; }
        [[nodiscard]] runtime::scheduler& scheduler() noexcept { return scheduler_; }

      private:
        std::unique_ptr<manifests::nes::nes_system> sys_;
        // PPU (frame source), CPU, APU, and -- for a cart with an on-board sound chip
        // (Sunsoft 5B, ...) -- the expansion audio chip as a fourth entry.
        std::vector<chips::ichip*> chip_view_{};
        runtime::scheduler scheduler_;
        mnemos::video_region region_;
        double target_fps_;
        std::vector<frontend_sdk::spec_field> spec_{};
        // Advertised input ports: the frontend routes a mouse-driven light gun to
        // whichever port reports a lightgun format (port 2 when the Zapper is on).
        frontend_sdk::session_capability_info session_{};
        // drain_audio scratch: the APU queues interleaved stereo (mono duplicated
        // to both lanes); it is resampled to the output frame rate, carrying the
        // fractional remainder so the long-term rate stays exact. exp_buf_ holds the
        // cartridge expansion audio (if any), resampled and summed into the same mix.
        std::vector<std::int16_t> apu_buf_{};
        std::vector<std::int16_t> exp_buf_{};
        std::vector<std::int16_t> mix_buf_{};
        double audio_frac_{0.0};
        double lp_state_{0.0}; // one-pole low-pass state: the 2A03's ~14 kHz output filter
    };

} // namespace mnemos::apps::player::adapters::nes
