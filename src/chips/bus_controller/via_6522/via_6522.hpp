#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::bus_controller {

    // MOS 6522 Versatile Interface Adapter (VIA).
    //
    // Two 8-bit ports with DDR and
    // input latching, two 16-bit timers (T1 free-run/one-shot with PB7 output, T2
    // timed/pulse-count), an 8-bit shift register, the CA1/CA2/CB1/CB2 handshake
    // lines, and the IFR/IER interrupt logic with a composite /IRQ output. tick()
    // advances one phi2 cycle; all time-domain behaviour evolves on tick.
    //
    // The C64's 1541 disk drive uses two of these (VIA1 = IEC serial port, VIA2 =
    // disk mechanism + GCR byte). CA2/CB2 *output* modes are not modelled — the
    // 1541 never drives them — only their input-edge IFR side.
    class via_6522 final : public ibus_controller, public immio {
      public:
        // Host wiring. Ports are read live through callbacks (input pins sampled at
        // access time unless input latching is enabled); output callbacks fire when
        // the driven state changes; irq_edge fires on /IRQ transitions.
        struct config final {
            std::function<std::uint8_t()> read_port_a;
            std::function<std::uint8_t()> read_port_b;
            std::function<void(std::uint8_t)> write_port_a;
            std::function<void(std::uint8_t)> write_port_b;
            std::function<void(bool)> irq_edge;
        };

        via_6522() { configure(config{}); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Apply host wiring. Re-initialises chip state (like power-on).
        using ichip::configure; // un-hide base overload (gcc -Woverloaded-virtual)
        void configure(config cfg);

        // Bus access over the low 4 bits of the address.
        [[nodiscard]] std::uint8_t read(std::uint8_t address);
        void write(std::uint8_t address, std::uint8_t value);

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return read(static_cast<std::uint8_t>(offset));
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            write(static_cast<std::uint8_t>(offset), value);
        }

        // External pin events (the device wires these from the bus/mechanism).
        void ca1_edge(bool level); // active edge per PCR bit0 -> IFR.CA1 (+ IRA latch)
        void cb1_edge(bool level); // active edge per PCR bit4 -> IFR.CB1 (+ IRB latch, SR clock)
        void cb2_edge(bool level); // input edge per PCR[7:5] -> IFR.CB2
        void pb6_pulse();          // negative PB6 edge: T2 pulse-count decrement

        [[nodiscard]] bool irq_asserted() const noexcept;

        // Live pin-level port values (output bits from the latch via DDR, input
        // bits from the read callback / pulled high), with the T1 PB7 override.
        [[nodiscard]] std::uint8_t port_a_pins() const;
        [[nodiscard]] std::uint8_t port_b_pins() const;

        // The bits each port actively drives (output latch masked by DDR); input
        // bits read 0. Devices derive driven lines (IEC, stepper, ...) from these.
        [[nodiscard]] std::uint8_t port_a_output() const noexcept {
            return static_cast<std::uint8_t>(ora_ & ddra_);
        }
        [[nodiscard]] std::uint8_t port_b_output() const noexcept {
            return static_cast<std::uint8_t>(orb_ & ddrb_);
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        struct timer1 final {
            std::uint16_t counter{0xFFFFU};
            std::uint16_t latch{0xFFFFU};
            bool pb7_level{true};
            bool reload_phase{}; // phantom-$FFFF cycle before reload (continuous)
            bool underflowed_once{};
        };

        struct timer2 final {
            std::uint16_t counter{0xFFFFU};
            std::uint8_t latch_low{0xFFU};
            bool underflowed_once{};
        };

        struct shifter final {
            std::uint8_t value{};
            std::uint8_t count{};
            std::uint8_t phase{}; // phi2 sub-cycle accumulator for timed modes
            bool active{};
        };

        void publish_irq();
        void set_ifr(std::uint8_t bits);   // raise sources
        void clear_ifr(std::uint8_t bits); // acknowledge sources
        [[nodiscard]] std::uint8_t composite_ifr() const noexcept;
        void t1_step();
        void t2_step();
        void sr_step();
        void publish_port_a();
        void publish_port_b();
        [[nodiscard]] std::uint8_t read_pa_live() const;
        [[nodiscard]] std::uint8_t read_pb_live() const;
        [[nodiscard]] std::uint8_t sr_mode() const noexcept;

        config cfg_{};

        std::uint8_t ora_{};
        std::uint8_t orb_{};
        std::uint8_t ddra_{};
        std::uint8_t ddrb_{};
        std::uint8_t ira_latched_{0xFFU};
        std::uint8_t irb_latched_{0xFFU};

        timer1 t1_{};
        timer2 t2_{};
        shifter sr_{};

        std::uint8_t acr_{};
        std::uint8_t pcr_{};
        std::uint8_t ifr_{};
        std::uint8_t ier_{};

        bool ca1_prev_{true};
        bool cb1_prev_{true};
        bool cb2_prev_{true};
        bool irq_out_{};

        std::array<register_descriptor, 4> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::bus_controller
