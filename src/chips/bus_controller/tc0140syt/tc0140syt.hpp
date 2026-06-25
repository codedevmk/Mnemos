#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::bus_controller {

    // Taito TC0140SYT sound-communication latch. F2 boards use it as a
    // nibble-wide command/reply mailbox between the 68000 and sound Z80.
    class tc0140syt final : public ibus_controller {
      public:
        tc0140syt();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        [[nodiscard]] std::uint8_t& command_latch(unsigned port) noexcept;
        [[nodiscard]] const std::uint8_t& command_latch(unsigned port) const noexcept;
        [[nodiscard]] std::uint8_t& reply_latch(unsigned port) noexcept;
        [[nodiscard]] const std::uint8_t& reply_latch(unsigned port) const noexcept;
        [[nodiscard]] bool& command_pending(unsigned port) noexcept;
        [[nodiscard]] const bool& command_pending(unsigned port) const noexcept;
        [[nodiscard]] bool& reply_pending(unsigned port) noexcept;
        [[nodiscard]] const bool& reply_pending(unsigned port) const noexcept;

        [[nodiscard]] std::uint8_t& main_port() noexcept { return main_port_; }
        [[nodiscard]] const std::uint8_t& main_port() const noexcept { return main_port_; }
        [[nodiscard]] std::uint8_t& sound_port() noexcept { return sound_port_; }
        [[nodiscard]] const std::uint8_t& sound_port() const noexcept { return sound_port_; }
        [[nodiscard]] bool& main_read_high() noexcept { return main_read_high_; }
        [[nodiscard]] const bool& main_read_high() const noexcept { return main_read_high_; }
        [[nodiscard]] bool& main_write_high() noexcept { return main_write_high_; }
        [[nodiscard]] const bool& main_write_high() const noexcept { return main_write_high_; }
        [[nodiscard]] bool& sound_read_high() noexcept { return sound_read_high_; }
        [[nodiscard]] const bool& sound_read_high() const noexcept { return sound_read_high_; }
        [[nodiscard]] bool& sound_write_high() noexcept { return sound_write_high_; }
        [[nodiscard]] const bool& sound_write_high() const noexcept { return sound_write_high_; }

        [[nodiscard]] std::uint8_t status() const noexcept;
        [[nodiscard]] std::uint8_t command_pending_mask() const noexcept;
        [[nodiscard]] std::uint8_t reply_pending_mask() const noexcept;
        [[nodiscard]] std::uint8_t phase_mask() const noexcept;
        void clear_all_pending() noexcept;

        void note_command_nmi_pulse() noexcept { ++command_nmi_pulses_; }
        void note_command_write(unsigned port, std::uint8_t value) noexcept;
        void note_command_read(unsigned port, std::uint8_t value) noexcept;
        void note_reply_write(unsigned port, std::uint8_t value) noexcept;
        void note_reply_read(unsigned port, std::uint8_t value) noexcept;
        void set_command_nmi_pulses(std::uint64_t count) noexcept {
            command_nmi_pulses_ = count;
        }
        [[nodiscard]] std::uint64_t command_nmi_pulses() const noexcept {
            return command_nmi_pulses_;
        }
        [[nodiscard]] std::uint64_t command_write_count(unsigned port) const noexcept {
            return command_write_count_[port_index(port)];
        }
        [[nodiscard]] std::uint64_t command_read_count(unsigned port) const noexcept {
            return command_read_count_[port_index(port)];
        }
        [[nodiscard]] std::uint64_t reply_write_count(unsigned port) const noexcept {
            return reply_write_count_[port_index(port)];
        }
        [[nodiscard]] std::uint64_t reply_read_count(unsigned port) const noexcept {
            return reply_read_count_[port_index(port)];
        }
        [[nodiscard]] std::uint64_t clear_count() const noexcept { return clear_count_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        [[nodiscard]] static std::size_t port_index(unsigned port) noexcept {
            return port < port_count ? static_cast<std::size_t>(port) : 0U;
        }

        static constexpr std::size_t port_count = 2U;

        std::array<std::uint8_t, port_count> command_latch_{};
        std::array<std::uint8_t, port_count> reply_latch_{};
        std::array<bool, port_count> command_pending_{};
        std::array<bool, port_count> reply_pending_{};
        std::uint8_t main_port_{};
        std::uint8_t sound_port_{};
        bool main_read_high_{};
        bool main_write_high_{};
        bool sound_read_high_{};
        bool sound_write_high_{};
        std::uint64_t command_nmi_pulses_{};
        std::array<std::uint64_t, port_count> command_write_count_{};
        std::array<std::uint64_t, port_count> command_read_count_{};
        std::array<std::uint64_t, port_count> reply_write_count_{};
        std::array<std::uint64_t, port_count> reply_read_count_{};
        std::uint64_t clear_count_{};
        std::uint8_t last_command_write_port_{};
        std::uint8_t last_command_write_value_{0xFFU};
        std::uint8_t last_command_read_port_{};
        std::uint8_t last_command_read_value_{0xFFU};
        std::uint8_t last_reply_write_port_{};
        std::uint8_t last_reply_write_value_{0xFFU};
        std::uint8_t last_reply_read_port_{};
        std::uint8_t last_reply_read_value_{0xFFU};

        std::array<register_descriptor, 31> register_view_{};
        instrumentation::introspection_builder introspection_{};
    };

} // namespace mnemos::chips::bus_controller
