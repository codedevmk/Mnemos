#pragma once

#include "chip.hpp"
#include "d64_image.hpp"
#include "iec_bus.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::chips::storage::c1541 {

    // Protocol-level ("synthetic") Commodore 1541 disk drive.
    //
    // Ported from the Emu reference core (ADR 0006). It answers the standard
    // KERNAL serial routines on the IEC bus at byte granularity — no drive CPU,
    // VIAs, or GCR — serving files straight out of a mounted .d64. This is the
    // proven path that makes LOAD"*",8,1 / LOAD"$",8 work; the cycle-accurate
    // drive (needed for fastloaders/protection) is separate.
    //
    // The command + channel-serving logic is exercised directly via the debug_*
    // helpers; tick() drives the bit-level IEC handshake the real KERNAL speaks.
    class synthetic_drive final : public istorage {
      public:
        static constexpr std::uint8_t default_device = 8U;
        static constexpr std::uint8_t channel_count = 16U;

        explicit synthetic_drive(std::uint8_t device_id = default_device) : device_(device_id) {}

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Wiring + media.
        void attach_bus(iec_bus& bus) noexcept { bus_ = &bus; }
        void set_device(std::uint8_t device_id) noexcept {
            device_ = static_cast<std::uint8_t>(device_id & 0x0FU);
        }
        [[nodiscard]] std::uint8_t device() const noexcept { return device_; }
        [[nodiscard]] bool mount(std::span<const std::uint8_t> d64);
        void unmount() noexcept;
        [[nodiscard]] bool mounted() const noexcept { return disk_.loaded(); }

        // Protocol-logic test/inspection surface (bypasses the bit-level handshake).
        void debug_command(std::uint8_t command);    // an ATN command byte
        void debug_filename_byte(std::uint8_t byte); // a filename byte during OPEN
        void debug_unlisten();                       // UNLISTEN: commit a pending OPEN
        [[nodiscard]] std::optional<std::uint8_t> debug_pull_byte(); // next TALK byte

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        struct channel final {
            std::vector<std::uint8_t> data;
            std::size_t pos{};
            bool open{};
        };

        // Bit-level IEC handshake state machine (driven by tick).
        enum class link_mode : std::uint8_t { idle, receiving, talking };

        void process_command(std::uint8_t command);
        void process_listen_payload(std::uint8_t byte);
        void load_into_channel(std::uint8_t sa);
        void serial_tick();
        void pull(iec_bus::line l, bool low) noexcept;
        [[nodiscard]] bool sense(iec_bus::line l) const noexcept;

        iec_bus* bus_{};
        std::uint8_t device_{default_device};
        d64_image disk_{};

        std::array<channel, channel_count> channels_{};
        std::vector<std::uint8_t> open_name_{}; // filename collected during OPEN
        std::uint8_t open_sa_{};
        bool listen_addressed_{};
        bool talk_addressed_{};
        bool open_pending_{};
        std::uint8_t listen_sa_{};
        std::uint8_t talk_sa_{};

        // Serial handshake working state.
        link_mode link_{link_mode::idle};
        std::uint8_t serial_phase_{};
        std::uint8_t serial_bit_{};
        std::uint8_t serial_shift_{};
        bool atn_prev_{true}; // previous ATN line level (released)
        bool clk_prev_{true}; // previous controller CLK level
        bool under_atn_{};    // current byte is an ATN command

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::storage::c1541
