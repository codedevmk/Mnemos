#include "rs232.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::peripheral {

    chip_metadata rs232::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "RS-232",
            .family = "uart",
            .klass = chip_class::peripheral,
            .revision = 1U,
        };
    }

    void rs232::reset(reset_kind /*kind*/) {
        txd_ = true;
        rx_active_ = false;
        rx_timer_ = 0U;
        rx_bit_ = 0U;
        rx_shift_ = 0U;

        rxd_ = true;
        tx_active_ = false;
        tx_timer_ = 0U;
        tx_bit_ = 0U;
        tx_shift_ = 0U;
    }

    // Sample the C64's TXD line into a byte. Idle = mark; a falling edge to space
    // is the start bit. We sample each data bit at its centre: 1.5 bit-times after
    // the start edge for bit 0, then one bit-time apart, LSB first.
    void rs232::step_capture() noexcept {
        if (!rx_active_) {
            if (!txd_) { // start bit (space)
                rx_active_ = true;
                rx_bit_ = 0U;
                rx_shift_ = 0U;
                rx_timer_ = cycles_per_bit_ + cycles_per_bit_ / 2U; // centre of bit 0
            }
            return;
        }

        if (rx_timer_ > 0U) {
            --rx_timer_;
        }
        if (rx_timer_ != 0U) {
            return;
        }
        if (rx_bit_ < data_bits_) {
            if (txd_) { // mark = 1
                rx_shift_ = static_cast<std::uint8_t>(rx_shift_ | (1U << rx_bit_));
            }
            ++rx_bit_;
            rx_timer_ = cycles_per_bit_; // next data bit, or the stop-bit centre
            return;
        }
        // Stop-bit centre: deliver the byte (lenient framing) and idle.
        if (byte_sink_) {
            byte_sink_(rx_shift_);
        }
        rx_active_ = false;
    }

    // Shift a byte out on the RXD line to the C64: start bit (space), data bits
    // LSB first, then a stop bit (mark). The start edge pulses the FLAG sink.
    void rs232::step_generate() {
        if (!tx_active_) {
            std::uint8_t b = 0U;
            if (byte_source_ && byte_source_(b)) {
                tx_active_ = true;
                tx_shift_ = b;
                tx_bit_ = 0U;
                rxd_ = false; // start bit (space)
                tx_timer_ = cycles_per_bit_;
                if (flag_sink_) {
                    flag_sink_(); // start-bit falling edge -> CIA2 /FLAG
                }
            } else {
                rxd_ = true; // idle mark
            }
            return;
        }

        if (tx_timer_ > 0U) {
            --tx_timer_;
        }
        if (tx_timer_ != 0U) {
            return;
        }
        ++tx_bit_;
        if (tx_bit_ <= data_bits_) {
            rxd_ = ((tx_shift_ >> (tx_bit_ - 1U)) & 1U) != 0U; // data bit, LSB first
            tx_timer_ = cycles_per_bit_;
        } else if (tx_bit_ == data_bits_ + 1U) {
            rxd_ = true; // stop bit (mark)
            tx_timer_ = cycles_per_bit_;
        } else {
            tx_active_ = false; // frame complete; next byte pulled next cycle
            rxd_ = true;
        }
    }

    void rs232::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            step_capture();
            step_generate();
        }
    }

    void rs232::save_state(state_writer& writer) const {
        writer.u32(cycles_per_bit_);
        writer.u8(data_bits_);
        writer.boolean(txd_);
        writer.boolean(rx_active_);
        writer.u32(rx_timer_);
        writer.u8(rx_bit_);
        writer.u8(rx_shift_);
        writer.boolean(rxd_);
        writer.boolean(tx_active_);
        writer.u32(tx_timer_);
        writer.u8(tx_bit_);
        writer.u8(tx_shift_);
    }

    void rs232::load_state(state_reader& reader) {
        cycles_per_bit_ = reader.u32();
        data_bits_ = reader.u8();
        txd_ = reader.boolean();
        rx_active_ = reader.boolean();
        rx_timer_ = reader.u32();
        rx_bit_ = reader.u8();
        rx_shift_ = reader.u8();
        rxd_ = reader.boolean();
        tx_active_ = reader.boolean();
        tx_timer_ = reader.u32();
        tx_bit_ = reader.u8();
        tx_shift_ = reader.u8();
    }

    instrumentation::ichip_introspection& rs232::introspection() noexcept { return introspection_; }

    namespace {
        [[maybe_unused]] const auto rs232_registration =
            register_factory("commodore.rs232", chip_class::peripheral,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<rs232>(); });
    } // namespace

} // namespace mnemos::chips::peripheral
