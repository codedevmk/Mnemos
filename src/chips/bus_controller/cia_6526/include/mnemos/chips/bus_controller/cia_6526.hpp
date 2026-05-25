#pragma once

#include <mnemos/chips/common/chip.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::bus_controller {

    // MOS 6526 Complex Interface Adapter (CIA).
    //
    // Ported from the Emu clean-room reference core (ADR 0006; Emu's ADR-0008).
    // Full feature surface: two 8-bit ports with DDR, two 16-bit interval timers
    // with cascade/pin-output and the silicon-accurate force-load + start-delay
    // pipelines, a BCD time-of-day clock with alarm, an 8-bit shift register with
    // SP/CNT pins, and the NMOS edge-triggered interrupt control with a 1-φ2 /IRQ
    // propagation delay. tick() advances one φ2 cycle; all time-domain behaviour
    // evolves on tick, never on register access.
    class cia_6526 final : public i_bus_controller, public i_mmio {
      public:
        enum class revision : std::uint8_t {
            nmos_6526, // edge-triggered IR (breadbin default)
            hmos_8521, // level-driven IR (selectable; behaves as NMOS for now)
        };

        // Host wiring. Ports are read live through callbacks (input pins sampled
        // at access time); output callbacks fire when the driven state changes;
        // irq_edge fires on /IRQ transitions. Unset callbacks: inputs pull high.
        struct config final {
            std::function<std::uint8_t()> read_port_a;
            std::function<std::uint8_t()> read_port_b;
            std::function<void(std::uint8_t)> write_port_a;
            std::function<void(std::uint8_t)> write_port_b;
            std::function<void(bool)> irq_edge;
            std::uint32_t tod_tick_hz{1'000'000U}; // φ2 cycles per second
            std::uint32_t tod_src_hz{60U};         // TOD source frequency (50/60)
            revision rev{revision::nmos_6526};
        };

        cia_6526() { configure(config{}); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        // Apply host wiring + TOD rates. Re-initialises chip state (like power-on).
        void configure(config cfg);

        // Bus access over the low 4 bits of the address. read is non-const: the
        // ICR read clears its latch, and reading TOD HR latches the digits.
        [[nodiscard]] std::uint8_t read(std::uint8_t address);
        void write(std::uint8_t address, std::uint8_t value);

        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override {
            return read(static_cast<std::uint8_t>(offset));
        }
        void mmio_write(std::uint16_t offset, std::uint8_t value) override {
            write(static_cast<std::uint8_t>(offset), value);
        }

        // External pin events.
        void flag_edge();              // FLAG negative edge
        void cnt_edge(bool new_level); // CNT level change (timers + shift register)
        void sp_level(bool new_level); // SP pin level (sampled on rising CNT)

        [[nodiscard]] bool irq_asserted() const noexcept { return irq_out_; }
        [[nodiscard]] std::uint8_t pb_timer_bits() const noexcept;

        // The live pin-level value of port A: output bits from the latch (masked by
        // DDRA), input bits from the read callback or pulled high. The C64 derives
        // the VIC bank from CIA2's PA0-1 this way (the write_port_a callback only
        // reports the output latch, not the composed pins).
        [[nodiscard]] std::uint8_t port_a_pins() const;

        // The bits port A actively drives (output latch masked by DDRA); input bits
        // read 0. The C64 derives the IEC ATN/CLK/DATA out lines from CIA2 PA3-5
        // this way, so floating (input) pins never pull the bus.
        [[nodiscard]] std::uint8_t port_a_output() const noexcept;
        // The bits port B actively drives (output latch masked by DDRB). The C64
        // keyboard scan reads the matrix against CIA1's driven row/column strobes.
        [[nodiscard]] std::uint8_t port_b_output() const noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        struct timer_state final {
            std::uint16_t counter{};
            std::uint16_t latch{};
            std::uint8_t cr{};
            bool running{};
            bool pulsing_pb{};
            bool toggle_pb{};
            bool underflow{};
            std::uint8_t force_load_phase{};
            std::uint8_t start_delay{};
        };

        struct tod_state final {
            std::uint8_t ten{}, sec{}, min{}, hr{};
            std::uint8_t alm_ten{}, alm_sec{}, alm_min{}, alm_hr{};
            std::uint8_t ltch_ten{}, ltch_sec{}, ltch_min{}, ltch_hr{};
            bool latched{};
            bool write_frozen{};
            std::uint8_t wr_ten{}, wr_sec{}, wr_min{}, wr_hr{};
            std::uint32_t divider{};
            std::uint32_t divider_reload{};
            std::uint32_t src_hz{};
            std::uint32_t phase{};
            std::uint32_t phase_reload{};
        };

        struct sdr_state final {
            std::uint8_t shift_reg{};
            std::uint8_t sdr_write{};
            bool output_mode{};
            bool shifting{};
            std::uint8_t bit_count{};
            bool sp_level{true};
            bool cnt_prev{true};
            bool pending_load{};
        };

        void irq_pin_update();
        void icr_raise(std::uint8_t bits);
        [[nodiscard]] std::uint8_t port_b_driven() const noexcept;
        void publish_port_a();
        void publish_port_b();
        [[nodiscard]] std::uint8_t read_pa_live();
        [[nodiscard]] std::uint8_t read_pb_live();
        void timer_handle_underflow(timer_state& t, bool is_timer_a);
        void timer_step(timer_state& t, unsigned count, bool is_timer_a);
        void tod_bcd_advance();
        void sdr_step_output_bit();
        void sdr_step_input_bit();

        config cfg_{};

        std::uint8_t pra_out_{};
        std::uint8_t prb_out_{};
        std::uint8_t ddra_{};
        std::uint8_t ddrb_{};

        timer_state timer_a_{};
        timer_state timer_b_{};
        tod_state tod_{};
        sdr_state sdr_{};

        std::uint8_t icr_latch_{};
        std::uint8_t imr_{};
        bool irq_line_{};
        bool irq_out_{};

        bool flag_prev_{true};
        bool cnt_prev_{true};

        std::array<register_descriptor, 4> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::bus_controller
