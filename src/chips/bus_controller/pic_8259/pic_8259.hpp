#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace mnemos::chips::bus_controller {

    // Intel 8259A programmable interrupt controller (NEC second-source:
    // uPD71059), the single-master subset: the ICW1/ICW2/[ICW3]/[ICW4] init
    // sequence, the OCW1 interrupt mask, OCW2 EOI (non-specific + specific),
    // OCW3 register-read select, edge- or level-triggered request sensing
    // (ICW1 LTIM), fixed IR0-highest priority, and the AEOI mode. Cascade
    // wiring, polling, rotating priority, and special-mask mode are out of
    // scope until a board needs them.
    //
    // The host wires INT through `set_int_callback` (level semantics; fired on
    // change) and answers the CPU's interrupt-acknowledge cycle by calling
    // `acknowledge()`, which resolves the highest pending request to its
    // vector exactly as the INTA pulse pair does. Request inputs arrive via
    // `set_irq_line(line, level)`: a rising edge latches the request (edge
    // mode); dropping the line withdraws a not-yet-acknowledged request in
    // either mode, matching the hardware's "IR must stay high until INTA"
    // rule that boards exploit to cancel a stale request.
    //
    // tick() is a no-op -- the chip is purely reactive; it sits on a bus, so
    // it is an ibus_controller and exposes its two A0-selected ports through
    // read()/write().
    class pic_8259 final : public ibus_controller {
      public:
        using int_callback = std::function<void(bool asserted)>;

        pic_8259() { reset(reset_kind::power_on); }

        // ichip
        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // INT output (to the CPU's INTR pin). Fired on level change only.
        void set_int_callback(int_callback cb) noexcept { int_cb_ = std::move(cb); }

        // The two CPU-visible ports; a0 is the chip's A0 pin (0 = command/
        // status, 1 = data/mask).
        [[nodiscard]] std::uint8_t read(std::uint8_t a0) noexcept;
        void write(std::uint8_t a0, std::uint8_t value) noexcept;

        // IR0-IR7 request inputs.
        void set_irq_line(unsigned line, bool level) noexcept;

        // The CPU's INTA cycle: resolves the highest-priority unmasked request,
        // moves it IRR->ISR (unless AEOI), updates INT, and returns its vector.
        // With nothing pending (a withdrawn request) returns the spurious
        // vector, base + 7, per the hardware.
        [[nodiscard]] std::uint8_t acknowledge() noexcept;

        [[nodiscard]] bool int_asserted() const noexcept { return int_line_; }

        // Programmed state, for boards/tests.
        [[nodiscard]] std::uint8_t vector_base() const noexcept { return vector_base_; }
        [[nodiscard]] std::uint8_t irr() const noexcept { return irr_; }
        [[nodiscard]] std::uint8_t isr() const noexcept { return isr_; }
        [[nodiscard]] std::uint8_t imr() const noexcept { return imr_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {
          public:
            explicit introspection_surface(pic_8259& owner) noexcept : registers_impl_(owner) {}
            [[nodiscard]] instrumentation::register_view* registers() override {
                return &registers_impl_;
            }

          private:
            class registers_impl final : public instrumentation::register_view {
              public:
                explicit registers_impl(pic_8259& owner) noexcept : owner_(&owner) {}
                [[nodiscard]] std::span<const register_descriptor> registers() override;

              private:
                pic_8259* owner_;
            };

            registers_impl registers_impl_;
        };

        // Where the next A0=1 write lands during the init sequence.
        enum class init_stage : std::uint8_t { idle, want_icw2, want_icw3, want_icw4 };

        [[nodiscard]] std::uint8_t pending_unmasked() const noexcept;
        [[nodiscard]] int highest_set_bit_priority(std::uint8_t bits) const noexcept;
        void update_int() noexcept;

        std::uint8_t irr_{};
        std::uint8_t isr_{};
        std::uint8_t imr_{};
        std::uint8_t vector_base_{};
        std::uint8_t lines_{}; // raw IR pin levels, for edge detection
        bool level_triggered_{};
        bool auto_eoi_{};
        bool expect_icw4_{};
        bool icw3_expected_{}; // ICW1 SNGL=0: an ICW3 sits in the sequence
        bool read_isr_{};      // OCW3 RR/RIS: A0=0 reads ISR instead of IRR
        init_stage init_stage_{init_stage::idle};
        bool int_line_{};

        int_callback int_cb_{};

        std::array<register_descriptor, 5> register_view_{};
        introspection_surface introspection_{*this};
    };

} // namespace mnemos::chips::bus_controller
