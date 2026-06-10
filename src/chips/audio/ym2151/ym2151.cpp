#include "ym2151.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::audio {

    chip_metadata ym2151::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "ym2151",
            .family = "opm",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void ym2151::reset(reset_kind /*kind*/) {
        registers_.fill(0U);
        address_ = 0U;
        timer_a_counter_ = 0U;
        timer_b_counter_ = 0U;
        timer_a_running_ = false;
        timer_b_running_ = false;
        timer_a_flag_ = false;
        timer_b_flag_ = false;
        irq_enable_a_ = false;
        irq_enable_b_ = false;
        prescale_64_ = 0U;
        timer_b_sub_ = 0U;
        busy_remaining_ = 0U;
        elapsed_ = 0U;
        const bool was_asserted = irq_line_;
        irq_line_ = false;
        if (was_asserted && irq_) {
            irq_(false);
        }
    }

    void ym2151::update_irq() noexcept {
        const bool asserted = (timer_a_flag_ && irq_enable_a_) || (timer_b_flag_ && irq_enable_b_);
        if (asserted != irq_line_) {
            irq_line_ = asserted;
            if (irq_) {
                irq_(asserted);
            }
        }
    }

    void ym2151::write_data(std::uint8_t value) noexcept {
        registers_[address_] = value;
        busy_remaining_ = busy_clocks;

        if (address_ == 0x14U) {
            // Timer control: bit0/1 run A/B (reload on the 0->1 edge),
            // bit2/3 IRQ enable A/B, bit4/5 write-one-to-clear flag A/B.
            // (bit7 CSM is stored but not acted on in this increment.)
            const bool run_a = (value & 0x01U) != 0U;
            const bool run_b = (value & 0x02U) != 0U;
            if (run_a && !timer_a_running_) {
                timer_a_counter_ = timer_a_load();
            }
            if (run_b && !timer_b_running_) {
                timer_b_counter_ = registers_[0x12];
            }
            timer_a_running_ = run_a;
            timer_b_running_ = run_b;
            irq_enable_a_ = (value & 0x04U) != 0U;
            irq_enable_b_ = (value & 0x08U) != 0U;
            if ((value & 0x10U) != 0U) {
                timer_a_flag_ = false;
            }
            if ((value & 0x20U) != 0U) {
                timer_b_flag_ = false;
            }
            update_irq();
        }
    }

    std::uint8_t ym2151::read_status() const noexcept {
        std::uint8_t status = 0U;
        if (timer_a_flag_) {
            status |= status_timer_a;
        }
        if (timer_b_flag_) {
            status |= status_timer_b;
        }
        if (busy_remaining_ != 0U) {
            status |= status_busy;
        }
        return status;
    }

    void ym2151::step_timer_a() noexcept {
        if (!timer_a_running_) {
            return;
        }
        if (++timer_a_counter_ >= 1024U) {
            timer_a_counter_ = timer_a_load();
            timer_a_flag_ = true;
            update_irq();
        }
    }

    void ym2151::step_timer_b() noexcept {
        if (!timer_b_running_) {
            return;
        }
        if (++timer_b_counter_ >= 256U) {
            timer_b_counter_ = registers_[0x12];
            timer_b_flag_ = true;
            update_irq();
        }
    }

    void ym2151::tick(std::uint64_t cycles) {
        elapsed_ += cycles;
        for (std::uint64_t c = 0; c < cycles; ++c) {
            if (busy_remaining_ != 0U) {
                --busy_remaining_;
            }
            if (++prescale_64_ == timer_a_step_clocks) {
                prescale_64_ = 0U;
                step_timer_a();
                if (++timer_b_sub_ == timer_b_step_clocks / timer_a_step_clocks) {
                    timer_b_sub_ = 0U;
                    step_timer_b();
                }
            }
        }
    }

    void ym2151::save_state(state_writer& writer) const {
        writer.bytes(registers_);
        writer.u8(address_);
        writer.u16(timer_a_counter_);
        writer.u16(timer_b_counter_);
        writer.boolean(timer_a_running_);
        writer.boolean(timer_b_running_);
        writer.boolean(timer_a_flag_);
        writer.boolean(timer_b_flag_);
        writer.boolean(irq_enable_a_);
        writer.boolean(irq_enable_b_);
        writer.boolean(irq_line_);
        writer.u32(prescale_64_);
        writer.u32(timer_b_sub_);
        writer.u32(busy_remaining_);
        writer.u64(elapsed_);
    }

    void ym2151::load_state(state_reader& reader) {
        reader.bytes(registers_);
        address_ = reader.u8();
        timer_a_counter_ = reader.u16();
        timer_b_counter_ = reader.u16();
        timer_a_running_ = reader.boolean();
        timer_b_running_ = reader.boolean();
        timer_a_flag_ = reader.boolean();
        timer_b_flag_ = reader.boolean();
        irq_enable_a_ = reader.boolean();
        irq_enable_b_ = reader.boolean();
        irq_line_ = reader.boolean();
        prescale_64_ = reader.u32();
        timer_b_sub_ = reader.u32();
        busy_remaining_ = reader.u32();
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& ym2151::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor>
    ym2151::introspection_surface::registers_impl::registers() {
        using fmt = register_value_format;
        ym2151& chip = *owner_;
        chip.register_view_[0] = {"ADDR", chip.address_, 8U, fmt::unsigned_integer};
        chip.register_view_[1] = {"STATUS", chip.read_status(), 8U, fmt::flags};
        chip.register_view_[2] = {"CLKA", chip.timer_a_load(), 10U, fmt::unsigned_integer};
        chip.register_view_[3] = {"CLKB", chip.registers_[0x12], 8U, fmt::unsigned_integer};
        chip.register_view_[4] = {"TA", chip.timer_a_counter_, 10U, fmt::unsigned_integer};
        chip.register_view_[5] = {"TB", chip.timer_b_counter_, 8U, fmt::unsigned_integer};
        return chip.register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ym2151_registration =
            register_factory("yamaha.ym2151", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ym2151>(); });
    } // namespace

} // namespace mnemos::chips::audio
