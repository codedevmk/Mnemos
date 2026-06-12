#pragma once

#include "chip.hpp"

#include <array>
#include <cstdint>
#include <functional>

namespace mnemos::chips {
    class state_writer;
    class state_reader;
    class ibus;
} // namespace mnemos::chips

namespace mnemos::chips::cpu {

    // Hitachi SH7604 on-chip peripherals, mapped at $FFFFFE00..$FFFFFFFF: the
    // interrupt controller (INTC), free-running timer (FRT), watchdog timer
    // (WDT), bus state controller (BSC), DMA controller (DMAC), serial
    // controller (SCI), and the cache/power control registers. One block is
    // integral to each SH-2 core, so the `sh2` chip owns one by composition and
    // intercepts the window before its external bus -- mapping it on a shared bus
    // would alias the master and slave CPUs' private peripheral state.
    //
    // Implemented behaviour covers the register-window shell plus the 32X-visible
    // SCI, FRT, WDT, DMAC, DIVU, and INTC request delivery. Unmodelled modules
    // still read and write back as storage until their behaviour lands.
    class sh2_peripherals final {
      public:
        static constexpr std::uint32_t window_base = 0xFFFFFE00U; // on-chip register base
        static constexpr std::uint32_t window_size = 0x200U;      // 512-byte window

        // True for an address inside the on-chip peripheral window.
        [[nodiscard]] static constexpr bool in_window(std::uint32_t addr) noexcept {
            return addr >= window_base;
        }

        // The highest on-chip interrupt the INTC currently presents (level 0 = none).
        // Derived from live register state with no side effects: the on-chip
        // sources are level-triggered and stay asserted until the handler clears
        // them, so the CPU may poll this every boundary.
        struct onchip_irq final {
            int level{};
            std::uint8_t vector{};
        };
        struct watchdog_reset_request final {
            bool asserted{};
            reset_kind kind{reset_kind::power_on};
        };

        // The DMAC moves data over the same bus the CPU sees; the owning sh2
        // supplies the handle when it attaches its bus.
        void set_bus(ibus* bus) noexcept { bus_ = bus; }

        // Module-request (CHCR.AR = 0) DREQ level per channel: the board answers
        // whether the requesting device still has data. Queried per transferred
        // unit, so a source whose read drains the device self-regulates.
        void set_dreq_query(std::function<bool(int channel)> query) noexcept {
            dreq_query_ = std::move(query);
        }
        void set_bus_wait_callback(
            std::function<int(std::uint32_t address, std::uint8_t bytes, bool locked)> cb) {
            bus_wait_ = std::move(cb);
        }

        // External serial input from the owning board/manifest. The SCI latches
        // the byte only while receiver-enable is set; pending RX/ERI interrupts
        // then flow through the normal INTC priority/vector registers.
        void sci_receive_byte(std::uint8_t value, std::uint8_t error_flags = 0U) noexcept;

        [[nodiscard]] std::uint8_t read8(std::uint32_t addr) const noexcept;
        void write8(std::uint32_t addr, std::uint8_t value) noexcept;
        // Advance time-driven peripherals by `cycles` SH-2 clocks, run any
        // active DMAC work, and return peripheral-generated wait cycles that
        // must stall the owning CPU.
        std::uint64_t tick(std::uint64_t cycles) noexcept;
        [[nodiscard]] onchip_irq pending_onchip_irq() const noexcept;
        [[nodiscard]] watchdog_reset_request consume_watchdog_reset() noexcept;
        // The external WDTOVF pin (low-pulsed for 128 cycles on a watchdog
        // overflow). The 32X leaves it unconnected; exposed so a board can wire
        // it. Transient -- not part of the save-state.
        [[nodiscard]] bool wdtovf_pin_asserted() const noexcept { return wdtovf_pin_cycles_ > 0; }
        [[nodiscard]] std::uint64_t consume_divu_access_wait(std::uint32_t addr,
                                                             bool is_read) noexcept;
        void reset() noexcept;
        void reset_preserving_watchdog_status() noexcept;

        void save_state(state_writer& writer) const;
        void load_state(state_reader& reader);

      private:
        // The output-compare register the TOCR.OCRS bit currently selects.
        [[nodiscard]] std::uint16_t selected_ocr() const noexcept {
            return (tocr_ & 0x10U) != 0U ? ocrb_ : ocra_;
        }
        void start_sci_transmit_if_ready() noexcept;

        std::array<std::uint8_t, window_size> regs_{};

        // SCI -- serial communication interface ($FE00-$FE05). This slice models
        // software-visible data/status flag generation with hardware-accurate
        // SSR read-then-write-0 clear semantics. NOT modelled (no 32X host
        // surface): the master<->slave serial LINK (TxD/RxD/SCK between the two
        // SH-2s -- a real board wire, but 32X software uses the COMM registers
        // for inter-CPU traffic), and bit-rate BRR/baud waveform pacing. Tracked
        // under X5; revisit only if a title is found driving the SCI link.
        std::uint8_t sci_smr_{};                    // FE00 serial mode
        std::uint8_t sci_brr_{0xFFU};               // FE01 bit-rate
        std::uint8_t sci_scr_{};                    // FE02 control
        std::uint8_t sci_tdr_{0xFFU};               // FE03 transmit data
        std::uint8_t sci_ssr_{0x84U};               // FE04 status (TDRE | TEND after reset)
        std::uint8_t sci_rdr_{};                    // FE05 receive data
        int sci_tx_cycles_{};                       // remaining coarse transmit-complete ticks
        mutable std::uint8_t sci_ssr_read_flags_{}; // SSR status bits observed by a read

        // FRT -- free-running timer ($FE10-$FE17). A 16-bit counter clocked off a
        // TCR-selected prescale of the SH-2 clock; matches against OCRA/OCRB and
        // wraps, latching status flags in FTCSR. Input capture (FICR) is not
        // modelled.
        std::uint16_t frc_{};                     // FE12/13 free-running counter
        std::uint16_t ocra_{0xFFFFU};             // FE14/15 output compare A
        std::uint16_t ocrb_{0xFFFFU};             // FE14/15 output compare B
        std::uint8_t tier_{0x01U};                // FE10 timer interrupt enable
        std::uint8_t ftcsr_{};                    // FE11 control/status flags
        std::uint8_t tcr_{};                      // FE16 timer control (clock select)
        std::uint8_t tocr_{0xE0U};                // FE17 output-compare control
        int frt_prescale_acc_{};                  // accumulated source clocks
        mutable std::uint8_t frt_temp_{};         // 16-bit access byte latch
        mutable std::uint8_t ftcsr_read_flags_{}; // flags observed by a read

        // INTC -- interrupt controller. IPRA covers DIVU/DMAC/WDT priorities;
        // IPRB covers SCI/FRT priorities. VCRA/B are SCI vectors, VCRC/D are FRT
        // vectors, and VCRWDT holds the WDT interval vector.
        std::uint16_t ipra_{};   // FEE2 interrupt priority A
        std::uint16_t iprb_{};   // FE60 interrupt priority B
        std::uint16_t vcra_{};   // FE62 SCI ERI/RXI vectors
        std::uint16_t vcrb_{};   // FE64 SCI TXI/TEI vectors
        std::uint16_t vcrc_{};   // FE66 FRT ICI/OCI vectors
        std::uint16_t vcrd_{};   // FE68 FRT OVI vector
        std::uint16_t vcrwdt_{}; // FEE4 WDT ITI vector

        // WDT -- watchdog / interval timer ($FE80-$FE83). An 8-bit counter
        // (WTCNT) clocked off a WTCSR-selected prescale; on overflow it sets the
        // interval-timer flag (WTCSR.OVF) or, in watchdog mode, RSTCSR.WOVF and
        // the optional internal reset request selected by RSTCSR.RSTE/RSTS.
        // Writes are keyed (a high-byte key selects the register).
        std::uint8_t wtcsr_{0x18U};     // FE80 control/status
        std::uint8_t wtcnt_{};          // FE80/81 counter
        std::uint8_t rstcsr_{0x1FU};    // FE82/83 reset control/status
        int wdt_prescale_acc_{};        // accumulated source clocks
        std::uint8_t wdt_key_{};        // latched high-byte key
        mutable bool wtcsr_ovf_read_{}; // WTCSR.OVF observed by a read
        bool watchdog_reset_pending_{}; // internal reset to consume
        reset_kind watchdog_reset_kind_{reset_kind::power_on};
        int wdtovf_pin_cycles_{}; // external WDTOVF pin low-pulse remaining (transient)

        // DMAC -- 2-channel DMA controller ($FF80-$FFAF channel regs, $FFB0 DMAOR).
        // Requests are arbitrated by DMAOR.PR, cycle-steal mode transfers one
        // unit per peripheral tick, and burst mode keeps the bus until the active
        // request drains. CHCR.TE/IE present transfer-end interrupts through INTC.
        // Bus-wait metering is reported through bus_wait_; exact shared-bus
        // contention remains board policy.
        struct dma_channel final {
            std::uint32_t sar{};  // source address
            std::uint32_t dar{};  // destination address
            std::uint32_t tcr{};  // transfer count
            std::uint32_t chcr{}; // channel control
        };
        std::array<dma_channel, 2> dma_{};
        std::array<std::uint32_t, 2> vcrdma_{}; // FFA0/FFA8 transfer-end vectors
        std::uint32_t dmaor_{};                 // DMA operation register (master enable + flags)
        ibus* bus_{}; // bus the DMAC transfers over (set by the owning sh2)
        std::function<bool(int)> dreq_query_{}; // module-request DREQ level per channel
        std::function<int(std::uint32_t, std::uint8_t, bool)> bus_wait_{};
        std::array<bool, 2> dreq_active_{};       // last sampled normalized request level
        std::array<bool, 2> dreq_edge_pending_{}; // latched inactive->active request edge
        std::uint8_t dmac_rr_top_{1U};            // DMAOR.PR first transfer after reset: ch1

        // Advance non-DMA time-driven on-chip units.
        void advance_time(std::uint64_t cycles) noexcept;
        void reset_watchdog_counter_control() noexcept;
        // Run active DMA work for this peripheral tick and return generated wait cycles.
        [[nodiscard]] std::uint64_t run_dmac() noexcept;

        // DIVU -- the division unit ($FF00-$FF1F, mirrored at $FF20-$FF3F).
        // Completing a DVDNT write starts a signed 32/32 divide (quotient ->
        // DVDNT/DVDNTL, remainder -> DVDNTH); completing a DVDNTL write starts
        // a signed 64/32 divide of DVDNTH:DVDNTL by DVSR. Divide-by-zero and a
        // quotient outside 32 bits set DVCR.OVF and yield the partial-iteration
        // hardware result. Results mature after 39 cycles, or after 6 cycles for
        // overflow; a DIVU register access while busy waits until completion.
        // DVDNTUH/UL shadow the last result. The overflow interrupt is delivered
        // when DVCR.OVFIE and VCRDIV are set.
        std::uint32_t dvsr_{};    // FF00 divisor
        std::uint32_t dvdnt_{};   // FF04 32-bit dividend / quotient
        std::uint32_t dvcr_{};    // FF08 control (bit 0 OVF, bit 1 OVFIE)
        std::uint16_t vcrdiv_{};  // FF0C overflow-interrupt vector
        std::uint32_t dvdnth_{};  // FF10 dividend high / remainder
        std::uint32_t dvdntl_{};  // FF14 dividend low / quotient
        std::uint32_t dvdntuh_{}; // FF18 shadow of the last remainder
        std::uint32_t dvdntul_{}; // FF1C shadow of the last quotient
        int divu_cycles_remaining_{};
        bool divu_pending_overflow_{};
        std::uint32_t divu_pending_quotient_{};
        std::uint32_t divu_pending_remainder_{};
        bool divu_post_write_{}; // a DIVU read right after a write costs +1 cycle
        void divu_run_32() noexcept;
        void divu_run_64() noexcept;
        void divu_start(bool overflow, std::uint32_t quotient, std::uint32_t remainder) noexcept;
        void divu_finish(bool overflow, std::uint32_t quotient, std::uint32_t remainder) noexcept;
        void divu_advance(std::uint64_t cycles) noexcept;
        void divu_complete_pending() noexcept;
    };

} // namespace mnemos::chips::cpu
