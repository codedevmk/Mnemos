#include "cia_6526.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>
#include <utility>

namespace mnemos::chips::bus_controller {
    namespace {

        constexpr std::uint8_t reg_pra = 0x0U;
        constexpr std::uint8_t reg_prb = 0x1U;
        constexpr std::uint8_t reg_ddra = 0x2U;
        constexpr std::uint8_t reg_ddrb = 0x3U;
        constexpr std::uint8_t reg_ta_lo = 0x4U;
        constexpr std::uint8_t reg_ta_hi = 0x5U;
        constexpr std::uint8_t reg_tb_lo = 0x6U;
        constexpr std::uint8_t reg_tb_hi = 0x7U;
        constexpr std::uint8_t reg_tod_ten = 0x8U;
        constexpr std::uint8_t reg_tod_sec = 0x9U;
        constexpr std::uint8_t reg_tod_min = 0xAU;
        constexpr std::uint8_t reg_tod_hr = 0xBU;
        constexpr std::uint8_t reg_sdr = 0xCU;
        constexpr std::uint8_t reg_icr = 0xDU;
        constexpr std::uint8_t reg_cra = 0xEU;
        constexpr std::uint8_t reg_crb = 0xFU;

        constexpr std::uint8_t icr_ta = 0x01U;
        constexpr std::uint8_t icr_tb = 0x02U;
        constexpr std::uint8_t icr_alarm = 0x04U;
        constexpr std::uint8_t icr_sdr = 0x08U;
        constexpr std::uint8_t icr_flag = 0x10U;
        constexpr std::uint8_t icr_ir = 0x80U;
        constexpr std::uint8_t icr_set = 0x80U;

        constexpr std::uint8_t cra_start = 0x01U;
        constexpr std::uint8_t cra_pbon = 0x02U;
        constexpr std::uint8_t cra_outmode = 0x04U;
        constexpr std::uint8_t cra_runmode = 0x08U;
        constexpr std::uint8_t cra_load = 0x10U;
        constexpr std::uint8_t cra_inmode = 0x20U;
        constexpr std::uint8_t cra_spmode = 0x40U;
        constexpr std::uint8_t cra_todin = 0x80U;

        constexpr std::uint8_t crb_start = 0x01U;
        constexpr std::uint8_t crb_pbon = 0x02U;
        constexpr std::uint8_t crb_outmode = 0x04U;
        constexpr std::uint8_t crb_runmode = 0x08U;
        constexpr std::uint8_t crb_load = 0x10U;
        constexpr std::uint8_t crb_inmode_mask = 0x60U;
        constexpr std::uint8_t crb_inmode_phi2 = 0x00U;
        constexpr std::uint8_t crb_inmode_cnt = 0x20U;
        constexpr std::uint8_t crb_inmode_ta = 0x40U;
        constexpr std::uint8_t crb_inmode_tacnt = 0x60U;
        constexpr std::uint8_t crb_alarm = 0x80U;

        // Increment one BCD digit-pair; rolls over to 0 when it passes limit_bcd.
        [[nodiscard]] std::uint8_t bcd_inc(std::uint8_t v, std::uint8_t limit_bcd) {
            if (v == limit_bcd) {
                return 0U;
            }
            std::uint8_t lo = v & 0x0FU;
            std::uint8_t hi = static_cast<std::uint8_t>(v >> 4U);
            if (lo == 9U) {
                lo = 0U;
                hi = static_cast<std::uint8_t>((hi + 1U) & 0x0FU);
            } else {
                lo = static_cast<std::uint8_t>(lo + 1U);
            }
            return static_cast<std::uint8_t>((hi << 4U) | lo);
        }

        // Hours BCD: bit 7 is AM/PM, wraps 01..12 toggling AM/PM on 11->12.
        [[nodiscard]] std::uint8_t hr_bcd_inc(std::uint8_t hr) {
            std::uint8_t ampm = hr & 0x80U;
            std::uint8_t h = hr & 0x1FU;
            std::uint8_t lo = h & 0x0FU;
            auto hi = static_cast<std::uint8_t>((h >> 4U) & 0x01U);
            if (lo == 9U) {
                lo = 0U;
                hi = static_cast<std::uint8_t>(hi + 1U);
            } else {
                lo = static_cast<std::uint8_t>(lo + 1U);
            }
            h = static_cast<std::uint8_t>((hi << 4U) | lo);
            if (h == 0x12U) {
                ampm ^= 0x80U;
            } else if (h == 0x13U) {
                h = 0x01U;
            }
            return static_cast<std::uint8_t>(ampm | (h & 0x1FU));
        }

    } // namespace

    chip_metadata cia_6526::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "6526",
            .family = "CIA",
            .klass = chip_class::bus_controller,
            .revision = 1U,
        };
    }

    void cia_6526::configure(config cfg) {
        cfg_ = std::move(cfg);
        reset(reset_kind::power_on);
    }

    void cia_6526::reset(reset_kind /*kind*/) {
        pra_out_ = 0U;
        prb_out_ = 0U;
        ddra_ = 0U;
        ddrb_ = 0U;

        timer_a_ = timer_state{};
        timer_a_.counter = 0xFFFFU;
        timer_a_.latch = 0xFFFFU;
        timer_b_ = timer_state{};
        timer_b_.counter = 0xFFFFU;
        timer_b_.latch = 0xFFFFU;

        tod_ = tod_state{};
        tod_.src_hz = cfg_.tod_src_hz != 0U ? cfg_.tod_src_hz : 60U;
        tod_.divider_reload = cfg_.tod_tick_hz / tod_.src_hz;
        tod_.divider = tod_.divider_reload;
        tod_.phase_reload = 6U;
        tod_.phase = 6U;

        sdr_ = sdr_state{};

        icr_latch_ = 0U;
        imr_ = 0U;
        irq_line_ = false;
        irq_out_ = false;
        flag_prev_ = true;
        cnt_prev_ = true;

        if (cfg_.irq_edge) {
            cfg_.irq_edge(false);
        }
        publish_port_a();
        publish_port_b();
        publish_sp(true);
    }

    void cia_6526::irq_pin_update() {
        const bool asserted = irq_line_;
        if (irq_out_ != asserted) {
            irq_out_ = asserted;
            if (cfg_.irq_edge) {
                cfg_.irq_edge(asserted);
            }
        }
    }

    void cia_6526::icr_raise(std::uint8_t bits) {
        const auto edge = static_cast<std::uint8_t>(bits & 0x1FU);
        icr_latch_ = static_cast<std::uint8_t>(icr_latch_ | edge);
        if ((edge & imr_) != 0U) {
            irq_line_ = true;
        }
    }

    std::uint8_t cia_6526::port_b_driven() const noexcept {
        auto v = prb_out_;
        if ((timer_a_.cr & cra_pbon) != 0U) {
            v = static_cast<std::uint8_t>(v & ~0x40U);
            const bool level =
                (timer_a_.cr & cra_outmode) != 0U ? timer_a_.toggle_pb : timer_a_.pulsing_pb;
            if (level) {
                v = static_cast<std::uint8_t>(v | 0x40U);
            }
        }
        if ((timer_b_.cr & crb_pbon) != 0U) {
            v = static_cast<std::uint8_t>(v & ~0x80U);
            const bool level =
                (timer_b_.cr & crb_outmode) != 0U ? timer_b_.toggle_pb : timer_b_.pulsing_pb;
            if (level) {
                v = static_cast<std::uint8_t>(v | 0x80U);
            }
        }
        return v;
    }

    void cia_6526::publish_port_a() {
        if (cfg_.write_port_a) {
            cfg_.write_port_a(pra_out_);
        }
    }

    void cia_6526::publish_port_b() {
        if (cfg_.write_port_b) {
            cfg_.write_port_b(port_b_driven());
        }
    }

    void cia_6526::publish_sp(bool level) {
        if (cfg_.write_sp) {
            cfg_.write_sp(level);
        }
    }

    std::uint8_t cia_6526::read_pa_live() {
        const std::uint8_t pins = cfg_.read_port_a ? cfg_.read_port_a() : 0xFFU;
        return static_cast<std::uint8_t>((pra_out_ & ddra_) |
                                         (pins & static_cast<std::uint8_t>(~ddra_)));
    }

    std::uint8_t cia_6526::port_a_pins() const {
        const std::uint8_t pins = cfg_.read_port_a ? cfg_.read_port_a() : 0xFFU;
        return static_cast<std::uint8_t>((pra_out_ & ddra_) |
                                         (pins & static_cast<std::uint8_t>(~ddra_)));
    }

    std::uint8_t cia_6526::port_a_output() const noexcept {
        return static_cast<std::uint8_t>(pra_out_ & ddra_);
    }

    std::uint8_t cia_6526::port_b_output() const noexcept {
        return static_cast<std::uint8_t>(prb_out_ & ddrb_);
    }

    std::uint8_t cia_6526::read_pb_live() {
        const std::uint8_t pins = cfg_.read_port_b ? cfg_.read_port_b() : 0xFFU;
        const std::uint8_t latched = port_b_driven();
        return static_cast<std::uint8_t>((latched & ddrb_) |
                                         (pins & static_cast<std::uint8_t>(~ddrb_)));
    }

    void cia_6526::timer_handle_underflow(timer_state& t, bool is_timer_a) {
        t.underflow = true;
        t.counter = t.latch;

        const std::uint8_t pbon = is_timer_a ? cra_pbon : crb_pbon;
        const std::uint8_t outmode = is_timer_a ? cra_outmode : crb_outmode;
        if ((t.cr & pbon) != 0U) {
            if ((t.cr & outmode) != 0U) {
                t.toggle_pb = !t.toggle_pb;
            } else {
                t.pulsing_pb = true; // cleared next tick
            }
            publish_port_b();
        }

        icr_raise(is_timer_a ? icr_ta : icr_tb);

        const std::uint8_t runmode = is_timer_a ? cra_runmode : crb_runmode;
        if ((t.cr & runmode) != 0U) {
            const std::uint8_t start = is_timer_a ? cra_start : crb_start;
            t.cr = static_cast<std::uint8_t>(t.cr & ~start);
            t.running = false;
        }
    }

    void cia_6526::timer_step(timer_state& t, unsigned count, bool is_timer_a) {
        const bool pulse_decaying = t.pulsing_pb;
        t.pulsing_pb = false;
        t.underflow = false;

        const std::uint8_t pbon = is_timer_a ? cra_pbon : crb_pbon;
        const std::uint8_t outmode = is_timer_a ? cra_outmode : crb_outmode;
        if (pulse_decaying && (t.cr & pbon) != 0U && (t.cr & outmode) == 0U) {
            publish_port_b();
        }

        // Force-load pipeline: three-cycle suspension, counter loaded on the tick
        // the phase reaches zero (first underflow at C_w + N + 3).
        if (t.force_load_phase > 0U) {
            t.force_load_phase--;
            if (t.force_load_phase == 0U) {
                t.counter = t.latch;
            }
            t.start_delay = 0U;
            return;
        }

        // Start-bit one-cycle gating: a 0->1 START skips the first decrement.
        if (t.start_delay > 0U) {
            t.start_delay--;
            return;
        }

        if (!t.running || count == 0U) {
            return;
        }

        while (count-- > 0U) {
            // The counter occupies each value -- including 0 -- for one cycle;
            // underflow/reload happens on the cycle AFTER reaching 0, so the
            // continuous-mode period is latch+1 (the 6526 datasheet / Lorenz
            // model; the sibling VIA implements the same shape). Reloading on
            // the same cycle as the decrement-to-zero made every period one
            // cycle short and 0 unreadable.
            if (t.counter == 0U) {
                timer_handle_underflow(t, is_timer_a);
                if (!t.running) {
                    break;
                }
            } else {
                --t.counter;
            }
        }
    }

    void cia_6526::tod_bcd_advance() {
        tod_.ten = bcd_inc(tod_.ten, 0x09U);
        if (tod_.ten != 0U) {
            return;
        }
        tod_.sec = bcd_inc(tod_.sec, 0x59U);
        if (tod_.sec != 0U) {
            return;
        }
        tod_.min = bcd_inc(tod_.min, 0x59U);
        if (tod_.min != 0U) {
            return;
        }
        tod_.hr = hr_bcd_inc(tod_.hr);
    }

    void cia_6526::sdr_step_output_bit() {
        if (!sdr_.shifting) {
            if (sdr_.pending_load) {
                sdr_.shift_reg = sdr_.sdr_write;
                sdr_.pending_load = false;
                sdr_.shifting = true;
                sdr_.bit_count = 0U;
            } else {
                return;
            }
        }
        const bool next_sp = (sdr_.shift_reg & 0x80U) != 0U;
        if (sdr_.sp_level != next_sp) {
            sdr_.sp_level = next_sp;
            publish_sp(next_sp);
        }
        sdr_.shift_reg = static_cast<std::uint8_t>(sdr_.shift_reg << 1U);
        sdr_.bit_count = static_cast<std::uint8_t>(sdr_.bit_count + 1U);
        if (sdr_.bit_count >= 8U) {
            sdr_.shifting = false;
            sdr_.bit_count = 0U;
            icr_raise(icr_sdr);
        }
    }

    void cia_6526::sdr_step_input_bit() {
        sdr_.shift_reg =
            static_cast<std::uint8_t>((sdr_.shift_reg << 1U) | (sdr_.sp_level ? 1U : 0U));
        sdr_.bit_count = static_cast<std::uint8_t>(sdr_.bit_count + 1U);
        if (sdr_.bit_count >= 8U) {
            sdr_.bit_count = 0U;
            sdr_.sdr_write = sdr_.shift_reg;
            icr_raise(icr_sdr);
        }
    }

    std::uint8_t cia_6526::read(std::uint8_t address) {
        switch (address & 0x0FU) {
        case reg_pra:
            return read_pa_live();
        case reg_prb:
            return read_pb_live();
        case reg_ddra:
            return ddra_;
        case reg_ddrb:
            return ddrb_;
        case reg_ta_lo:
            return static_cast<std::uint8_t>(timer_a_.counter & 0xFFU);
        case reg_ta_hi:
            return static_cast<std::uint8_t>(timer_a_.counter >> 8U);
        case reg_tb_lo:
            return static_cast<std::uint8_t>(timer_b_.counter & 0xFFU);
        case reg_tb_hi:
            return static_cast<std::uint8_t>(timer_b_.counter >> 8U);
        case reg_tod_ten:
            if (tod_.latched) {
                tod_.latched = false;
                return tod_.ltch_ten;
            }
            return tod_.ten;
        case reg_tod_sec:
            return tod_.latched ? tod_.ltch_sec : tod_.sec;
        case reg_tod_min:
            return tod_.latched ? tod_.ltch_min : tod_.min;
        case reg_tod_hr:
            if (!tod_.latched) {
                tod_.latched = true;
                tod_.ltch_ten = tod_.ten;
                tod_.ltch_sec = tod_.sec;
                tod_.ltch_min = tod_.min;
                tod_.ltch_hr = tod_.hr;
            }
            return tod_.ltch_hr;
        case reg_sdr:
            return sdr_.sdr_write;
        case reg_icr: {
            // Read returns the latch + IR bit sampled before the clear; the latch,
            // the IR flip-flop, and the /IRQ pin all drop combinationally here.
            auto v = icr_latch_;
            if (irq_line_) {
                v = static_cast<std::uint8_t>(v | icr_ir);
            }
            icr_latch_ = 0U;
            irq_line_ = false;
            irq_pin_update();
            return v;
        }
        case reg_cra:
            return timer_a_.cr;
        case reg_crb:
            return timer_b_.cr;
        default:
            return 0xFFU;
        }
    }

    void cia_6526::write(std::uint8_t address, std::uint8_t value) {
        switch (address & 0x0FU) {
        case reg_pra:
            if (pra_out_ != value) {
                pra_out_ = value;
                publish_port_a();
            }
            return;
        case reg_prb:
            if (prb_out_ != value) {
                prb_out_ = value;
                publish_port_b();
            }
            return;
        case reg_ddra:
            if (ddra_ != value) {
                ddra_ = value;
                publish_port_a();
            }
            return;
        case reg_ddrb:
            if (ddrb_ != value) {
                ddrb_ = value;
                publish_port_b();
            }
            return;

        case reg_ta_lo:
            timer_a_.latch = static_cast<std::uint16_t>((timer_a_.latch & 0xFF00U) | value);
            return;
        case reg_ta_hi:
            timer_a_.latch = static_cast<std::uint16_t>((timer_a_.latch & 0x00FFU) |
                                                        (static_cast<std::uint16_t>(value) << 8U));
            if (!timer_a_.running) {
                timer_a_.counter = timer_a_.latch;
            }
            return;
        case reg_tb_lo:
            timer_b_.latch = static_cast<std::uint16_t>((timer_b_.latch & 0xFF00U) | value);
            return;
        case reg_tb_hi:
            timer_b_.latch = static_cast<std::uint16_t>((timer_b_.latch & 0x00FFU) |
                                                        (static_cast<std::uint16_t>(value) << 8U));
            if (!timer_b_.running) {
                timer_b_.counter = timer_b_.latch;
            }
            return;

        case reg_tod_ten:
            if ((timer_b_.cr & crb_alarm) != 0U) {
                tod_.alm_ten = static_cast<std::uint8_t>(value & 0x0FU);
            } else {
                if (tod_.write_frozen) {
                    tod_.ten = static_cast<std::uint8_t>(value & 0x0FU);
                    tod_.sec = tod_.wr_sec;
                    tod_.min = tod_.wr_min;
                    tod_.hr = tod_.wr_hr;
                    tod_.write_frozen = false;
                } else {
                    tod_.ten = static_cast<std::uint8_t>(value & 0x0FU);
                }
                tod_.divider = tod_.divider_reload;
                tod_.phase = tod_.phase_reload;
            }
            return;
        case reg_tod_sec:
            if ((timer_b_.cr & crb_alarm) != 0U) {
                tod_.alm_sec = static_cast<std::uint8_t>(value & 0x7FU);
            } else if (tod_.write_frozen) {
                tod_.wr_sec = static_cast<std::uint8_t>(value & 0x7FU);
            } else {
                tod_.sec = static_cast<std::uint8_t>(value & 0x7FU);
            }
            return;
        case reg_tod_min:
            if ((timer_b_.cr & crb_alarm) != 0U) {
                tod_.alm_min = static_cast<std::uint8_t>(value & 0x7FU);
            } else if (tod_.write_frozen) {
                tod_.wr_min = static_cast<std::uint8_t>(value & 0x7FU);
            } else {
                tod_.min = static_cast<std::uint8_t>(value & 0x7FU);
            }
            return;
        case reg_tod_hr:
            if ((timer_b_.cr & crb_alarm) != 0U) {
                tod_.alm_hr = static_cast<std::uint8_t>(value & 0x9FU);
            } else {
                tod_.write_frozen = true;
                tod_.wr_hr = static_cast<std::uint8_t>(value & 0x9FU);
                tod_.wr_sec = tod_.sec;
                tod_.wr_min = tod_.min;
            }
            return;

        case reg_sdr:
            sdr_.sdr_write = value;
            if (sdr_.output_mode) {
                if (sdr_.shifting) {
                    sdr_.pending_load = true;
                } else {
                    sdr_.shift_reg = value;
                    sdr_.shifting = true;
                    sdr_.bit_count = 0U;
                }
            }
            return;

        case reg_icr: {
            const auto mask = static_cast<std::uint8_t>(value & 0x1FU);
            if ((value & icr_set) != 0U) {
                imr_ = static_cast<std::uint8_t>(imr_ | mask);
            } else {
                imr_ = static_cast<std::uint8_t>(imr_ & ~mask);
            }
            return;
        }

        case reg_cra: {
            const bool was_running = timer_a_.running;
            const std::uint8_t old_cr = timer_a_.cr;
            if ((value & cra_load) != 0U) {
                timer_a_.force_load_phase = 3U;
                value = static_cast<std::uint8_t>(value & ~cra_load);
            }
            timer_a_.cr = value;
            timer_a_.running = (value & cra_start) != 0U;
            if (((old_cr ^ value) & cra_pbon) != 0U) {
                publish_port_b();
            }
            if (((old_cr ^ value) & cra_todin) != 0U) {
                tod_.phase_reload = (value & cra_todin) != 0U ? 5U : 6U;
                tod_.phase = tod_.phase_reload;
            }
            const bool was_output_mode = sdr_.output_mode;
            sdr_.output_mode = (value & cra_spmode) != 0U;
            if (was_output_mode && !sdr_.output_mode) {
                sdr_.shifting = false;
                sdr_.pending_load = false;
                sdr_.clock_ff = false;
                sdr_.bit_count = 0U;
                if (!sdr_.sp_level) {
                    sdr_.sp_level = true;
                    publish_sp(true);
                }
            }
            if (!was_running && timer_a_.running && timer_a_.force_load_phase == 0U &&
                timer_a_.counter == 0U) {
                timer_a_.counter = timer_a_.latch;
            }
            if (!was_running && timer_a_.running && timer_a_.force_load_phase == 0U) {
                timer_a_.start_delay = 1U;
            }
            return;
        }

        case reg_crb: {
            const bool was_running = timer_b_.running;
            const std::uint8_t old_cr = timer_b_.cr;
            if ((value & crb_load) != 0U) {
                timer_b_.force_load_phase = 3U;
                value = static_cast<std::uint8_t>(value & ~crb_load);
            }
            timer_b_.cr = value;
            timer_b_.running = (value & crb_start) != 0U;
            if (((old_cr ^ value) & crb_pbon) != 0U) {
                publish_port_b();
            }
            if (!was_running && timer_b_.running && timer_b_.force_load_phase == 0U &&
                timer_b_.counter == 0U) {
                timer_b_.counter = timer_b_.latch;
            }
            if (!was_running && timer_b_.running && timer_b_.force_load_phase == 0U) {
                timer_b_.start_delay = 1U;
            }
            return;
        }

        default:
            return;
        }
    }

    void cia_6526::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            irq_pin_update(); // 1-φ2 delayed publication of the IR flip-flop

            const bool cnt_now = cnt_prev_;
            const bool cnt_rising = false; // CNT edges arrive via cnt_edge()

            const unsigned ta_count = (timer_a_.cr & cra_inmode) != 0U ? 0U : 1U;
            timer_step(timer_a_, ta_count, true);

            unsigned tb_count = 0U;
            switch (timer_b_.cr & crb_inmode_mask) {
            case crb_inmode_phi2:
                tb_count = 1U;
                break;
            case crb_inmode_cnt:
                tb_count = cnt_rising ? 1U : 0U;
                break;
            case crb_inmode_ta:
                tb_count = timer_a_.underflow ? 1U : 0U;
                break;
            case crb_inmode_tacnt:
                tb_count = (timer_a_.underflow && cnt_now) ? 1U : 0U;
                break;
            default:
                break;
            }
            timer_step(timer_b_, tb_count, false);

            if (timer_a_.underflow && sdr_.output_mode) {
                // The serial shifter is clocked by an internal half-rate
                // flip-flop on TA underflow, independent of CRA.PBON -- gating
                // it on the PB6 output toggle hung SDR drivers that never
                // enable the port-B pin.
                sdr_.clock_ff = !sdr_.clock_ff;
                if (sdr_.clock_ff) {
                    sdr_step_output_bit();
                }
            }

            if (tod_.divider_reload != 0U && !tod_.write_frozen) {
                if (--tod_.divider == 0U) {
                    tod_.divider = tod_.divider_reload;
                    if (tod_.phase_reload != 0U && --tod_.phase == 0U) {
                        tod_.phase = tod_.phase_reload;
                        tod_bcd_advance();
                        if (tod_.ten == tod_.alm_ten && tod_.sec == tod_.alm_sec &&
                            tod_.min == tod_.alm_min && tod_.hr == tod_.alm_hr) {
                            icr_raise(icr_alarm);
                        }
                    }
                }
            }
        }
    }

    void cia_6526::flag_edge() { icr_raise(icr_flag); }

    void cia_6526::cnt_edge(bool new_level) {
        const bool rising = !cnt_prev_ && new_level;
        cnt_prev_ = new_level;
        sdr_.cnt_prev = new_level;
        if (!rising) {
            return;
        }

        if ((timer_a_.cr & cra_inmode) != 0U && timer_a_.running) {
            timer_step(timer_a_, 1U, true);
        }
        if ((timer_b_.cr & crb_inmode_mask) == crb_inmode_cnt && timer_b_.running) {
            timer_step(timer_b_, 1U, false);
        }
        if (!sdr_.output_mode) {
            sdr_step_input_bit();
        }
    }

    void cia_6526::sp_level(bool new_level) { sdr_.sp_level = new_level; }

    std::uint8_t cia_6526::pb_timer_bits() const noexcept {
        std::uint8_t v = 0U;
        if ((timer_a_.cr & cra_pbon) != 0U) {
            const bool level =
                (timer_a_.cr & cra_outmode) != 0U ? timer_a_.toggle_pb : timer_a_.pulsing_pb;
            if (level) {
                v = static_cast<std::uint8_t>(v | 0x40U);
            }
        }
        if ((timer_b_.cr & crb_pbon) != 0U) {
            const bool level =
                (timer_b_.cr & crb_outmode) != 0U ? timer_b_.toggle_pb : timer_b_.pulsing_pb;
            if (level) {
                v = static_cast<std::uint8_t>(v | 0x80U);
            }
        }
        return v;
    }

    void cia_6526::save_state(state_writer& writer) const {
        const auto write_timer = [&writer](const timer_state& t) {
            writer.u16(t.counter);
            writer.u16(t.latch);
            writer.u8(t.cr);
            writer.boolean(t.running);
            writer.boolean(t.pulsing_pb);
            writer.boolean(t.toggle_pb);
            writer.boolean(t.underflow);
            writer.u8(t.force_load_phase);
            writer.u8(t.start_delay);
        };

        writer.u8(pra_out_);
        writer.u8(prb_out_);
        writer.u8(ddra_);
        writer.u8(ddrb_);
        write_timer(timer_a_);
        write_timer(timer_b_);

        // TOD.
        writer.u8(tod_.ten);
        writer.u8(tod_.sec);
        writer.u8(tod_.min);
        writer.u8(tod_.hr);
        writer.u8(tod_.alm_ten);
        writer.u8(tod_.alm_sec);
        writer.u8(tod_.alm_min);
        writer.u8(tod_.alm_hr);
        writer.u8(tod_.ltch_ten);
        writer.u8(tod_.ltch_sec);
        writer.u8(tod_.ltch_min);
        writer.u8(tod_.ltch_hr);
        writer.boolean(tod_.latched);
        writer.boolean(tod_.write_frozen);
        writer.u8(tod_.wr_ten);
        writer.u8(tod_.wr_sec);
        writer.u8(tod_.wr_min);
        writer.u8(tod_.wr_hr);
        writer.u32(tod_.divider);
        writer.u32(tod_.divider_reload);
        writer.u32(tod_.src_hz);
        writer.u32(tod_.phase);
        writer.u32(tod_.phase_reload);

        // Shift register.
        writer.u8(sdr_.shift_reg);
        writer.u8(sdr_.sdr_write);
        writer.boolean(sdr_.output_mode);
        writer.boolean(sdr_.shifting);
        writer.boolean(sdr_.clock_ff);
        writer.u8(sdr_.bit_count);
        writer.boolean(sdr_.sp_level);
        writer.boolean(sdr_.cnt_prev);
        writer.boolean(sdr_.pending_load);

        // Interrupt control + pins.
        writer.u8(icr_latch_);
        writer.u8(imr_);
        writer.boolean(irq_line_);
        writer.boolean(irq_out_);
        writer.boolean(flag_prev_);
        writer.boolean(cnt_prev_);
    }

    void cia_6526::load_state(state_reader& reader) {
        const auto read_timer = [&reader](timer_state& t) {
            t.counter = reader.u16();
            t.latch = reader.u16();
            t.cr = reader.u8();
            t.running = reader.boolean();
            t.pulsing_pb = reader.boolean();
            t.toggle_pb = reader.boolean();
            t.underflow = reader.boolean();
            t.force_load_phase = reader.u8();
            t.start_delay = reader.u8();
        };

        pra_out_ = reader.u8();
        prb_out_ = reader.u8();
        ddra_ = reader.u8();
        ddrb_ = reader.u8();
        read_timer(timer_a_);
        read_timer(timer_b_);

        tod_.ten = reader.u8();
        tod_.sec = reader.u8();
        tod_.min = reader.u8();
        tod_.hr = reader.u8();
        tod_.alm_ten = reader.u8();
        tod_.alm_sec = reader.u8();
        tod_.alm_min = reader.u8();
        tod_.alm_hr = reader.u8();
        tod_.ltch_ten = reader.u8();
        tod_.ltch_sec = reader.u8();
        tod_.ltch_min = reader.u8();
        tod_.ltch_hr = reader.u8();
        tod_.latched = reader.boolean();
        tod_.write_frozen = reader.boolean();
        tod_.wr_ten = reader.u8();
        tod_.wr_sec = reader.u8();
        tod_.wr_min = reader.u8();
        tod_.wr_hr = reader.u8();
        tod_.divider = reader.u32();
        tod_.divider_reload = reader.u32();
        tod_.src_hz = reader.u32();
        tod_.phase = reader.u32();
        tod_.phase_reload = reader.u32();

        sdr_.shift_reg = reader.u8();
        sdr_.sdr_write = reader.u8();
        sdr_.output_mode = reader.boolean();
        sdr_.shifting = reader.boolean();
        sdr_.clock_ff = reader.boolean();
        sdr_.bit_count = reader.u8();
        sdr_.sp_level = reader.boolean();
        sdr_.cnt_prev = reader.boolean();
        sdr_.pending_load = reader.boolean();

        icr_latch_ = reader.u8();
        imr_ = reader.u8();
        irq_line_ = reader.boolean();
        irq_out_ = reader.boolean();
        flag_prev_ = reader.boolean();
        cnt_prev_ = reader.boolean();
    }

    instrumentation::ichip_introspection& cia_6526::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> cia_6526::register_snapshot() noexcept {
        register_view_[0] = {"TA", timer_a_.counter, 16U, register_value_format::unsigned_integer};
        register_view_[1] = {"TB", timer_b_.counter, 16U, register_value_format::unsigned_integer};
        register_view_[2] = {"ICR", icr_latch_, 8U, register_value_format::flags};
        register_view_[3] = {"IMR", imr_, 8U, register_value_format::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto cia_6526_registration = register_factory(
            "mos.6526", chip_class::bus_controller,
            []() -> std::unique_ptr<ichip> { return std::make_unique<cia_6526>(); });
    } // namespace

} // namespace mnemos::chips::bus_controller
