#pragma once

#include "chip.hpp"
#include "cpu_catch_up.hpp"
#include "introspection_adapters.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mnemos::chips::cpu {

    class i8080 final : public icpu, public cpu_catch_up<i8080> {
      public:
        enum class variant : std::uint8_t {
            intel_8080,
            intel_8085,
        };

        static constexpr std::uint8_t flag_c = 0x01U;
        static constexpr std::uint8_t flag_p = 0x04U;
        static constexpr std::uint8_t flag_ac = 0x10U;
        static constexpr std::uint8_t flag_z = 0x40U;
        static constexpr std::uint8_t flag_s = 0x80U;
        static constexpr std::uint8_t flag_const = 0x02U;

        struct registers final {
            std::uint8_t a{};
            std::uint8_t f{flag_const};
            std::uint16_t bc{};
            std::uint16_t de{};
            std::uint16_t hl{};
            std::uint16_t sp{};
            std::uint16_t pc{};
            bool interrupts_enabled{};
            bool halted{};
        };

        using port_in_fn = std::function<std::uint8_t(std::uint8_t port)>;
        using port_out_fn = std::function<void(std::uint8_t port, std::uint8_t value)>;
        using irq_vector_fn = std::function<std::uint8_t()>;

        i8080();

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return introspection_;
        }

        void attach_bus(ibus& bus) noexcept override { bus_ = &bus; }

        void set_variant(variant model) noexcept { variant_ = model; }
        [[nodiscard]] variant cpu_variant() const noexcept { return variant_; }
        void set_port_in(port_in_fn handler) noexcept { port_in_ = std::move(handler); }
        void set_port_out(port_out_fn handler) noexcept { port_out_ = std::move(handler); }
        void set_irq_vector(irq_vector_fn handler) noexcept { irq_vector_ = std::move(handler); }

        int step_instruction();

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return elapsed_; }
        [[nodiscard]] bool at_instruction_boundary() const noexcept { return true; }

        void set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }
        [[nodiscard]] bool interrupt_enabled() const noexcept { return interrupts_enabled_; }
        void set_reset_line(bool asserted) noexcept;
        [[nodiscard]] bool reset_line_held() const noexcept { return reset_line_; }

        void set_8085_interrupt_pending(std::uint8_t bits) noexcept {
            pending_8085_interrupts_ = static_cast<std::uint8_t>(bits & 0x07U);
        }
        [[nodiscard]] std::uint8_t serial_output() const noexcept { return serial_output_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        friend class cpu_catch_up<i8080>;

        [[nodiscard]] std::uint8_t rb(std::uint16_t address) noexcept;
        void wb(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t rw(std::uint16_t address) noexcept;
        void ww(std::uint16_t address, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t fetch8() noexcept;
        [[nodiscard]] std::uint16_t fetch16() noexcept;
        [[nodiscard]] std::uint8_t port_in8(std::uint8_t port);
        void port_out8(std::uint8_t port, std::uint8_t value);

        [[nodiscard]] std::uint8_t b() const noexcept { return static_cast<std::uint8_t>(bc_ >> 8U); }
        [[nodiscard]] std::uint8_t c() const noexcept { return static_cast<std::uint8_t>(bc_); }
        [[nodiscard]] std::uint8_t d() const noexcept { return static_cast<std::uint8_t>(de_ >> 8U); }
        [[nodiscard]] std::uint8_t e() const noexcept { return static_cast<std::uint8_t>(de_); }
        [[nodiscard]] std::uint8_t h() const noexcept { return static_cast<std::uint8_t>(hl_ >> 8U); }
        [[nodiscard]] std::uint8_t l() const noexcept { return static_cast<std::uint8_t>(hl_); }
        void set_b(std::uint8_t v) noexcept { bc_ = static_cast<std::uint16_t>((bc_ & 0x00FFU) | (v << 8U)); }
        void set_c(std::uint8_t v) noexcept { bc_ = static_cast<std::uint16_t>((bc_ & 0xFF00U) | v); }
        void set_d(std::uint8_t v) noexcept { de_ = static_cast<std::uint16_t>((de_ & 0x00FFU) | (v << 8U)); }
        void set_e(std::uint8_t v) noexcept { de_ = static_cast<std::uint16_t>((de_ & 0xFF00U) | v); }
        void set_h(std::uint8_t v) noexcept { hl_ = static_cast<std::uint16_t>((hl_ & 0x00FFU) | (v << 8U)); }
        void set_l(std::uint8_t v) noexcept { hl_ = static_cast<std::uint16_t>((hl_ & 0xFF00U) | v); }

        [[nodiscard]] std::uint8_t get_reg8(int reg) noexcept;
        void set_reg8(int reg, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint16_t* rp(int p) noexcept;
        [[nodiscard]] std::uint16_t rp_value_psw(int p) const noexcept;
        void set_rp_value_psw(int p, std::uint16_t value) noexcept;

        void push16(std::uint16_t value) noexcept;
        [[nodiscard]] std::uint16_t pop16() noexcept;
        [[nodiscard]] bool condition(int cc) const noexcept;
        void set_szp(std::uint8_t value) noexcept;
        void add_a(std::uint8_t value, std::uint8_t carry) noexcept;
        void sub_a(std::uint8_t value, std::uint8_t borrow, bool store) noexcept;
        void ana(std::uint8_t value) noexcept;
        void xra(std::uint8_t value) noexcept;
        void ora(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t inr(std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t dcr(std::uint8_t value) noexcept;
        void dad(std::uint16_t value) noexcept;
        void daa() noexcept;
        void alu(int op, std::uint8_t value) noexcept;
        void rim() noexcept;
        void sim() noexcept;
        void exec(std::uint8_t op);

        variant variant_{variant::intel_8080};
        std::uint8_t a_{};
        std::uint8_t f_{flag_const};
        std::uint16_t bc_{};
        std::uint16_t de_{};
        std::uint16_t hl_{};
        std::uint16_t sp_{0xFFFFU};
        std::uint16_t pc_{};
        bool interrupts_enabled_{};
        bool ei_pending_{};
        bool halted_{};
        bool irq_line_{};
        bool reset_line_{};
        std::uint8_t interrupt_mask_8085_{};
        std::uint8_t pending_8085_interrupts_{};
        std::uint8_t serial_output_{};
        std::uint64_t elapsed_{};
        int step_cycles_{};
        ibus* bus_{};
        port_in_fn port_in_{};
        port_out_fn port_out_{};
        irq_vector_fn irq_vector_{};
        instrumentation::introspection_builder introspection_{};
        std::function<void(std::uint32_t pc)> trace_callback_{};
        std::array<register_descriptor, 8> register_view_{};
    };

} // namespace mnemos::chips::cpu
