#pragma once

#include "chip.hpp"
#include "introspection_adapters.hpp"
#include "sh2.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::chips::cpu {

    // Hitachi SH7708 / SH-3 first-pass CPU surface.
    //
    // This exposes the SH7708 board identity and SH-3 control-state shell used
    // by late Irem hardware while delegating the currently implemented integer
    // instruction subset to the existing SH-2 core. It is intentionally a
    // conservative executable bridge: MMU/cache/timer/interrupt details beyond
    // the SH-2-compatible base are tracked as serializable registers, not yet
    // cycle-accurate SH-3 peripherals.
    class sh3 final : public icpu {
      public:
        enum class model : std::uint8_t {
            hd6417708s = 0,
        };

        struct registers final {
            sh2::registers core{};
            std::uint32_t mmucr{};
            std::uint32_t ccr{};
            std::uint32_t bcr1{};
            std::uint32_t bcr2{};
            std::uint32_t wcr1{};
            std::uint32_t wcr2{};
            std::uint32_t mcr{};
        };

        explicit sh3(model chip_model = model::hd6417708s);

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;
        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;
        void attach_bus(ibus& bus) noexcept override;

        int step_instruction();
        void grant_cycles(std::uint64_t cycles) noexcept { core_.grant_cycles(cycles); }
        [[nodiscard]] bool has_cycle_credit() const noexcept { return core_.has_cycle_credit(); }
        int step_credited_instruction() { return core_.step_credited_instruction(); }
        [[nodiscard]] std::uint64_t elapsed_cycles() const noexcept { return core_.elapsed_cycles(); }
        [[nodiscard]] std::uint32_t current_instruction_addr() const noexcept {
            return core_.current_instruction_addr();
        }

        void set_irq(int level, std::uint8_t vector) noexcept { core_.set_irq(level, vector); }
        void clear_irq() noexcept { core_.clear_irq(); }

        [[nodiscard]] registers cpu_registers() const noexcept;
        void set_registers(const registers& values) noexcept;

        [[nodiscard]] std::uint32_t read_onchip_register(std::uint32_t address) const noexcept;
        void write_onchip_register(std::uint32_t address, std::uint32_t value) noexcept;

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        void reset_local_registers() noexcept;

        model model_{model::hd6417708s};
        sh2 core_{};

        std::uint32_t mmucr_{};
        std::uint32_t ccr_{};
        std::uint32_t bcr1_{};
        std::uint32_t bcr2_{};
        std::uint32_t wcr1_{};
        std::uint32_t wcr2_{};
        std::uint32_t mcr_{};

        std::array<register_descriptor, 30> register_view_{};
        instrumentation::introspection_builder introspection_;
    };

} // namespace mnemos::chips::cpu
