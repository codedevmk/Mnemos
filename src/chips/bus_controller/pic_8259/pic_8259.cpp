#include "pic_8259.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::bus_controller {

    chip_metadata pic_8259::metadata() const noexcept {
        return {.manufacturer = "Intel",
                .part_number = "8259A",
                .family = "programmable interrupt controller",
                .klass = chip_class::bus_controller,
                .revision = 1U};
    }

    void pic_8259::tick(std::uint64_t /*cycles*/) {} // purely reactive

    void pic_8259::reset(reset_kind /*kind*/) {
        irr_ = isr_ = imr_ = 0U;
        vector_base_ = 0U;
        lines_ = 0U;
        level_triggered_ = auto_eoi_ = expect_icw4_ = read_isr_ = false;
        init_stage_ = init_stage::idle;
        const bool was_int = int_line_;
        int_line_ = false;
        if (was_int && int_cb_) {
            int_cb_(false);
        }
    }

    std::uint8_t pic_8259::pending_unmasked() const noexcept {
        return static_cast<std::uint8_t>(irr_ & ~imr_);
    }

    int pic_8259::highest_set_bit_priority(std::uint8_t bits) const noexcept {
        // Fixed priority: IR0 highest.
        for (int line = 0; line < 8; ++line) {
            if ((bits & (1U << static_cast<unsigned>(line))) != 0U) {
                return line;
            }
        }
        return -1;
    }

    void pic_8259::update_int() noexcept {
        // Fully-nested mode: a request interrupts only if it outranks every
        // in-service level.
        const int pending = highest_set_bit_priority(pending_unmasked());
        const int in_service = highest_set_bit_priority(isr_);
        const bool assert_int = pending >= 0 && (in_service < 0 || pending < in_service);
        if (assert_int != int_line_) {
            int_line_ = assert_int;
            if (int_cb_) {
                int_cb_(assert_int);
            }
        }
    }

    void pic_8259::set_irq_line(unsigned line, bool level) noexcept {
        if (line >= 8U) {
            return;
        }
        const auto bit = static_cast<std::uint8_t>(1U << line);
        const bool was_high = (lines_ & bit) != 0U;
        lines_ = static_cast<std::uint8_t>(level ? (lines_ | bit) : (lines_ & ~bit));
        if (level && (!was_high || level_triggered_)) {
            irr_ |= bit; // edge mode latches the rising edge; level mode follows
        } else if (!level) {
            // A request withdrawn before INTA is forgotten in both modes.
            irr_ &= static_cast<std::uint8_t>(~bit);
        }
        update_int();
    }

    std::uint8_t pic_8259::acknowledge() noexcept {
        const int line = highest_set_bit_priority(pending_unmasked());
        if (line < 0) {
            // Spurious acknowledge: the hardware answers IR7.
            return static_cast<std::uint8_t>(vector_base_ | 7U);
        }
        const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(line));
        if (!level_triggered_) {
            irr_ &= static_cast<std::uint8_t>(~bit);
        }
        if (!auto_eoi_) {
            isr_ |= bit;
        }
        update_int();
        return static_cast<std::uint8_t>(vector_base_ + static_cast<std::uint8_t>(line));
    }

    std::uint8_t pic_8259::read(std::uint8_t a0) noexcept {
        if ((a0 & 1U) != 0U) {
            return imr_; // OCW1 readback
        }
        return read_isr_ ? isr_ : irr_;
    }

    void pic_8259::write(std::uint8_t a0, std::uint8_t value) noexcept {
        if ((a0 & 1U) == 0U) {
            if ((value & 0x10U) != 0U) {
                // ICW1: restart the init sequence. Bit0 = IC4 expected,
                // bit1 = single (no ICW3), bit3 = level-triggered mode.
                expect_icw4_ = (value & 0x01U) != 0U;
                const bool single = (value & 0x02U) != 0U;
                level_triggered_ = (value & 0x08U) != 0U;
                imr_ = 0U;
                isr_ = 0U;
                irr_ = level_triggered_ ? lines_ : 0U;
                read_isr_ = false;
                auto_eoi_ = false;
                init_stage_ = init_stage::want_icw2;
                // Remember whether ICW3 is in the sequence: encode by jumping
                // straight from ICW2 to ICW4 when single.
                icw3_expected_ = !single;
                update_int();
                return;
            }
            if ((value & 0x08U) != 0U) {
                // OCW3: register-read select (RR=bit1, RIS=bit0). Poll and
                // special-mask commands are out of the modelled subset.
                if ((value & 0x02U) != 0U) {
                    read_isr_ = (value & 0x01U) != 0U;
                }
                return;
            }
            // OCW2: EOI commands. Bits 7:5 = R/SL/EOI.
            switch (value >> 5U) {
            case 0x1U: { // non-specific EOI: clear the highest in-service level
                const int line = highest_set_bit_priority(isr_);
                if (line >= 0) {
                    isr_ &= static_cast<std::uint8_t>(~(1U << static_cast<unsigned>(line)));
                }
                break;
            }
            case 0x3U: // specific EOI: clear the named level
                isr_ &= static_cast<std::uint8_t>(~(1U << (value & 0x07U)));
                break;
            default: // rotate commands: outside the modelled subset
                break;
            }
            update_int();
            return;
        }

        switch (init_stage_) {
        case init_stage::want_icw2:
            vector_base_ = static_cast<std::uint8_t>(value & 0xF8U);
            init_stage_ = icw3_expected_ ? init_stage::want_icw3
                          : expect_icw4_ ? init_stage::want_icw4
                                         : init_stage::idle;
            break;
        case init_stage::want_icw3: // cascade wiring: accepted, unused
            init_stage_ = expect_icw4_ ? init_stage::want_icw4 : init_stage::idle;
            break;
        case init_stage::want_icw4:
            auto_eoi_ = (value & 0x02U) != 0U;
            init_stage_ = init_stage::idle;
            break;
        case init_stage::idle:
            imr_ = value; // OCW1
            update_int();
            break;
        }
    }

    void pic_8259::save_state(state_writer& writer) const {
        writer.u8(irr_);
        writer.u8(isr_);
        writer.u8(imr_);
        writer.u8(vector_base_);
        writer.u8(lines_);
        writer.boolean(level_triggered_);
        writer.boolean(auto_eoi_);
        writer.boolean(expect_icw4_);
        writer.boolean(icw3_expected_);
        writer.boolean(read_isr_);
        writer.u8(static_cast<std::uint8_t>(init_stage_));
        writer.boolean(int_line_);
    }

    void pic_8259::load_state(state_reader& reader) {
        irr_ = reader.u8();
        isr_ = reader.u8();
        imr_ = reader.u8();
        vector_base_ = reader.u8();
        lines_ = reader.u8();
        level_triggered_ = reader.boolean();
        auto_eoi_ = reader.boolean();
        expect_icw4_ = reader.boolean();
        icw3_expected_ = reader.boolean();
        read_isr_ = reader.boolean();
        init_stage_ = static_cast<init_stage>(reader.u8());
        int_line_ = reader.boolean();
    }

    std::span<const register_descriptor>
    pic_8259::introspection_surface::registers_impl::registers() {
        owner_->register_view_ = {{
            {.name = "IRR", .value = owner_->irr_, .bit_width = 8},
            {.name = "ISR", .value = owner_->isr_, .bit_width = 8},
            {.name = "IMR", .value = owner_->imr_, .bit_width = 8},
            {.name = "BASE", .value = owner_->vector_base_, .bit_width = 8},
            {.name = "INT", .value = owner_->int_line_ ? 1U : 0U, .bit_width = 1},
        }};
        return owner_->register_view_;
    }

    instrumentation::ichip_introspection& pic_8259::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto pic_8259_registration = register_factory(
            "intel.8259a", chip_class::bus_controller,
            []() -> std::unique_ptr<ichip> { return std::make_unique<pic_8259>(); });
    } // namespace

} // namespace mnemos::chips::bus_controller
