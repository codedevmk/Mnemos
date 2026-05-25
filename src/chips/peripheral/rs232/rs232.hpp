#pragma once

#include "chip.hpp"

#include <cstdint>
#include <functional>

namespace mnemos::chips::peripheral {

    // Bit-level RS-232 UART for the C64 userport (the physical layer the KERNAL's
    // software RS-232 routines bit-bang against). It bridges single TXD/RXD line
    // levels to whole bytes in both directions:
    //
    //   from the C64 (TXD): the KERNAL drives the TXD line (CIA2 PA2) with a
    //       start bit (space/0), 8 data bits LSB-first, and a stop bit (mark/1)
    //       at the configured baud. The UART samples each bit at its centre,
    //       assembles the byte, and hands it to the byte sink (-> modem).
    //   to the C64 (RXD): a byte pulled from the byte source (<- modem) is
    //       shifted out on the RXD line (read on CIA2 PB0) as start + 8 data +
    //       stop bits; the start-bit falling edge pulses the FLAG sink (CIA2
    //       /FLAG) so the KERNAL's receive routine wakes up.
    //
    // Line levels follow the RS-232/TTL convention used here: true = mark (idle,
    // logic 1), false = space (logic 0). The baud is configured as cycles-per-bit
    // (the C64 sets it via a CIA timer the KERNAL programs from the baud rate;
    // matching that programmed rate end-to-end is data-gated on the KERNAL ROM).
    // Framing is the common 8N1; data/stop bit counts are configurable.
    class rs232 final : public iperipheral {
      public:
        static constexpr std::uint32_t default_cycles_per_bit = 104U; // ~9600 baud @ ~1 MHz

        rs232() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Baud / framing. cycles_per_bit must be >= 2 so a bit has a centre.
        void set_cycles_per_bit(std::uint32_t cycles) noexcept {
            cycles_per_bit_ = cycles < 2U ? 2U : cycles;
        }
        void set_data_bits(std::uint8_t bits) noexcept {
            data_bits_ = (bits >= 5U && bits <= 8U) ? bits : 8U;
        }
        [[nodiscard]] std::uint32_t cycles_per_bit() const noexcept { return cycles_per_bit_; }

        // ---- from the C64 (TXD sampling) ----
        // Set the TXD line level driven by the C64 (CIA2 PA2). Mark = idle/1.
        void set_txd(bool mark) noexcept { txd_ = mark; }
        // Delivered a fully framed byte received from the C64.
        void set_byte_sink(std::function<void(std::uint8_t)> sink) noexcept {
            byte_sink_ = std::move(sink);
        }

        // ---- to the C64 (RXD generation) ----
        // The RXD line level the C64 reads (CIA2 PB0). Mark = idle/1.
        [[nodiscard]] bool rxd() const noexcept { return rxd_; }
        // Pulled when the transmitter is idle: fill out with the next byte to send
        // to the C64 and return true, or return false if none is available.
        void set_byte_source(std::function<bool(std::uint8_t&)> source) noexcept {
            byte_source_ = std::move(source);
        }
        // Fired on each RXD start-bit falling edge (mark -> space) so the wiring
        // can pulse CIA2 /FLAG.
        void set_flag_sink(std::function<void()> sink) noexcept { flag_sink_ = std::move(sink); }

        // Introspection helpers (also used by the tests).
        [[nodiscard]] bool transmitting() const noexcept { return tx_active_; }
        [[nodiscard]] bool receiving() const noexcept { return rx_active_; }

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        void step_capture() noexcept; // sample TXD -> byte (from the C64)
        void step_generate();         // byte -> shift RXD (to the C64)

        std::uint32_t cycles_per_bit_{default_cycles_per_bit};
        std::uint8_t data_bits_{8U};

        // TXD capture (from the C64).
        bool txd_{true};   // line level (idle mark)
        bool rx_active_{}; // assembling a byte
        std::uint32_t rx_timer_{};
        std::uint8_t rx_bit_{};
        std::uint8_t rx_shift_{};

        // RXD generation (to the C64).
        bool rxd_{true};   // line level (idle mark)
        bool tx_active_{}; // shifting a byte
        std::uint32_t tx_timer_{};
        std::uint8_t tx_bit_{}; // 0 = start, 1..N = data, N+1 = stop
        std::uint8_t tx_shift_{};

        std::function<void(std::uint8_t)> byte_sink_{};
        std::function<bool(std::uint8_t&)> byte_source_{};
        std::function<void()> flag_sink_{};

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::peripheral
