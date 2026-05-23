#include <mnemos/chips/cpu/m6510.hpp>

#include <mnemos/chips/common/bus.hpp>
#include <mnemos/chips/common/chip_registry.hpp>

#include <memory>

namespace mnemos::chips::cpu {

    chip_metadata m6510::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "6510",
            .family = "6502",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void m6510::tick(std::uint64_t /*cycles*/) {
        // Opcode fetch/decode/execute is implemented in the next M1 task; the
        // core does not advance until then.
    }

    void m6510::reset(reset_kind kind) {
        // Functional reset: power-on starts from a cleared register file, while a
        // hard/soft reset preserves A/X/Y as the NMOS part does. Cycle-accurate
        // RES sequencing is part of the interrupt-handling task.
        if (kind == reset_kind::power_on) {
            registers_ = registers{};
        }

        registers_.sp = 0xFDU;
        set_flag(status_flag::irq_disable, true);
        set_flag(status_flag::unused, true);
        cycles_ = 0U;

        if (bus_ != nullptr) {
            const auto low = static_cast<std::uint16_t>(bus_->read8(reset_vector));
            const auto high = static_cast<std::uint16_t>(bus_->read8(reset_vector + 1U));
            registers_.pc =
                static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
        }
    }

    void m6510::save_state(state_writer& /*writer*/) const {
        // Serialization lands with the M1 save/load-state task.
    }

    void m6510::load_state(state_reader& /*reader*/) {
        // Deserialization lands with the M1 save/load-state task.
    }

    instrumentation::i_chip_introspection& m6510::introspection() noexcept {
        return introspection_;
    }

    void m6510::attach_bus(i_bus& bus) noexcept { bus_ = &bus; }

    bool m6510::flag(status_flag bit) const noexcept {
        const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(bit));
        return (registers_.p & mask) != 0U;
    }

    void m6510::set_flag(status_flag bit, bool value) noexcept {
        const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(bit));
        if (value) {
            registers_.p = static_cast<std::uint8_t>(registers_.p | mask);
        } else {
            registers_.p =
                static_cast<std::uint8_t>(registers_.p & static_cast<std::uint8_t>(~mask));
        }
    }

    namespace {
        [[maybe_unused]] const auto m6510_registration =
            register_factory("mos.6510", chip_class::cpu,
                             []() -> std::unique_ptr<i_chip> { return std::make_unique<m6510>(); });
    } // namespace

} // namespace mnemos::chips::cpu
