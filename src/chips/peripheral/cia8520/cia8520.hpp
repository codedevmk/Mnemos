#pragma once

#include "cia_6526.hpp"
#include "chip.hpp"
#include "introspection_adapters.hpp"

#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::peripheral {

    // CSG 8520 Complex Interface Adapter (CIA) -- the Amiga-era CIA.
    //
    // The 8520 is register-compatible with the MOS 6526 for the timer, TOD,
    // serial, port, and interrupt behavior Mnemos currently models. Keep the
    // public peripheral identity here, but delegate the shared state machine to
    // the 6526 implementation so fixes land once.
    class cia8520 final : public iperipheral, public immio {
      public:
        struct config final {
            std::function<std::uint8_t()> read_port_a;
            std::function<std::uint8_t()> read_port_b;
            std::function<void(std::uint8_t)> write_port_a;
            std::function<void(std::uint8_t)> write_port_b;
            std::function<void(bool)> irq_edge;
            std::uint32_t tod_tick_hz{715'909U}; // source clock per second (NTSC /E)
            std::uint32_t tod_src_hz{60U};       // TOD source frequency (50/60)
        };

        cia8520() { configure(config{}); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        using ichip::configure; // un-hide base overload (gcc -Woverloaded-virtual)
        void configure(config cfg);

        [[nodiscard]] std::uint8_t read(std::uint8_t address);
        void write(std::uint8_t address, std::uint8_t value);

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return read(static_cast<std::uint8_t>(offset));
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            write(static_cast<std::uint8_t>(offset), value);
        }

        void flag_edge();
        void cnt_edge(bool new_level);
        void sp_level(bool new_level);

        [[nodiscard]] bool irq_asserted() const noexcept { return core_.irq_asserted(); }
        [[nodiscard]] std::uint8_t pb_timer_bits() const noexcept {
            return core_.pb_timer_bits();
        }
        [[nodiscard]] std::uint8_t port_a_pins() const { return core_.port_a_pins(); }
        [[nodiscard]] std::uint8_t port_a_output() const noexcept {
            return core_.port_a_output();
        }
        [[nodiscard]] std::uint8_t port_b_output() const noexcept {
            return core_.port_b_output();
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        bus_controller::cia_6526 core_{};
        instrumentation::introspection_builder introspection_{};
    };

} // namespace mnemos::chips::peripheral
