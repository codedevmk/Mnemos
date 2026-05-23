#include <mnemos/chips/cpu/m6510.hpp>

#include <mnemos/chips/cpu/m6510/decode_table.hpp>

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

    void m6510::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            step_one_cycle();
            ++cycles_;
        }
    }

    void m6510::step_one_cycle() {
        if (tcu_ == 0U) {
            ir_ = read(registers_.pc++); // opcode fetch is cycle 1
            tcu_ = 1U;
            return;
        }

        const decoded& entry = decode_table()[ir_];
        switch (entry.kind) {
        case access_kind::implied:
            step_implied(entry);
            return;
        case access_kind::read:
            step_read(entry);
            return;
        default:
            // Unimplemented access kinds (illegal/jam slots for now) end here;
            // real micro-sequences are added in their own tasks.
            tcu_ = 0U;
            return;
        }
    }

    void m6510::step_implied(const decoded& entry) {
        static_cast<void>(read(registers_.pc)); // dummy read of next byte, no increment
        execute_implied(entry.op);
        tcu_ = 0U;
    }

    void m6510::step_read(const decoded& entry) {
        switch (entry.mode) {
        case addressing_mode::immediate:
            operand_ = read(registers_.pc++);
            execute_read(entry.op);
            tcu_ = 0U;
            return;
        case addressing_mode::zero_page:
            if (tcu_ == 1U) {
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            }
            operand_ = read(ea_);
            execute_read(entry.op);
            tcu_ = 0U;
            return;
        case addressing_mode::absolute:
            if (tcu_ == 1U) {
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            }
            if (tcu_ == 2U) {
                const auto high = static_cast<std::uint16_t>(read(registers_.pc++));
                ea_ = static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                tcu_ = 3U;
                return;
            }
            operand_ = read(ea_);
            execute_read(entry.op);
            tcu_ = 0U;
            return;
        default:
            tcu_ = 0U;
            return;
        }
    }

    void m6510::execute_read(operation op) noexcept {
        switch (op) {
        case operation::lda:
            op_lda(operand_);
            return;
        default:
            return;
        }
    }

    void m6510::execute_implied(operation op) noexcept {
        switch (op) {
        case operation::nop:
        default:
            return;
        }
    }

    void m6510::op_lda(std::uint8_t value) noexcept {
        registers_.a = value;
        set_nz(value);
    }

    void m6510::set_nz(std::uint8_t value) noexcept {
        set_flag(status_flag::zero, value == 0U);
        set_flag(status_flag::negative, (value & 0x80U) != 0U);
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

    std::uint8_t m6510::read(std::uint16_t address) noexcept {
        if (address == 0x0000U) {
            return port_ddr_;
        }
        if (address == 0x0001U) {
            const auto outputs = static_cast<std::uint8_t>(port_data_ & port_ddr_);
            const auto inputs =
                static_cast<std::uint8_t>(port_input_pull & static_cast<std::uint8_t>(~port_ddr_));
            return static_cast<std::uint8_t>(outputs | inputs);
        }
        return bus_ != nullptr ? bus_->read8(address) : static_cast<std::uint8_t>(0U);
    }

    void m6510::write(std::uint16_t address, std::uint8_t value) noexcept {
        if (address == 0x0000U) {
            port_ddr_ = value;
            return;
        }
        if (address == 0x0001U) {
            port_data_ = value;
            return;
        }
        if (bus_ != nullptr) {
            bus_->write8(address, value);
        }
    }

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
