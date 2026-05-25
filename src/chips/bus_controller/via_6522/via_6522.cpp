#include "via_6522.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>
#include <utility>

namespace mnemos::chips::bus_controller {
    namespace {

        // Register indices (address & 0x0F).
        constexpr std::uint8_t reg_orb = 0x0U;
        constexpr std::uint8_t reg_ora = 0x1U;
        constexpr std::uint8_t reg_ddrb = 0x2U;
        constexpr std::uint8_t reg_ddra = 0x3U;
        constexpr std::uint8_t reg_t1cl = 0x4U;
        constexpr std::uint8_t reg_t1ch = 0x5U;
        constexpr std::uint8_t reg_t1ll = 0x6U;
        constexpr std::uint8_t reg_t1lh = 0x7U;
        constexpr std::uint8_t reg_t2cl = 0x8U;
        constexpr std::uint8_t reg_t2ch = 0x9U;
        constexpr std::uint8_t reg_sr = 0xAU;
        constexpr std::uint8_t reg_acr = 0xBU;
        constexpr std::uint8_t reg_pcr = 0xCU;
        constexpr std::uint8_t reg_ifr = 0xDU;
        constexpr std::uint8_t reg_ier = 0xEU;
        constexpr std::uint8_t reg_ora_noh = 0xFU;

        // IFR / IER source bits.
        constexpr std::uint8_t irq_ca2 = 0x01U;
        constexpr std::uint8_t irq_ca1 = 0x02U;
        constexpr std::uint8_t irq_sr = 0x04U;
        constexpr std::uint8_t irq_cb2 = 0x08U;
        constexpr std::uint8_t irq_cb1 = 0x10U;
        constexpr std::uint8_t irq_t2 = 0x20U;
        constexpr std::uint8_t irq_t1 = 0x40U;
        constexpr std::uint8_t irq_master = 0x80U;
        constexpr std::uint8_t irq_src_mask = 0x7FU;

        // ACR bits.
        constexpr std::uint8_t acr_pa_latch = 0x01U;
        constexpr std::uint8_t acr_pb_latch = 0x02U;
        constexpr std::uint8_t acr_sr_mask = 0x1CU;
        constexpr std::uint8_t acr_t2_pulse = 0x20U;
        constexpr std::uint8_t acr_t1_cont = 0x40U;
        constexpr std::uint8_t acr_t1_pb7 = 0x80U;

        constexpr std::uint8_t pcr_ca1_pol = 0x01U;
        constexpr std::uint8_t pcr_cb1_pol = 0x10U;

        constexpr std::uint8_t pb7_bit = 0x80U;

    } // namespace

    chip_metadata via_6522::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "6522",
            .family = "VIA",
            .klass = chip_class::bus_controller,
            .revision = 1U,
        };
    }

    void via_6522::configure(config cfg) {
        cfg_ = std::move(cfg);
        reset(reset_kind::power_on);
    }

    void via_6522::reset(reset_kind /*kind*/) {
        ora_ = 0U;
        orb_ = 0U;
        ddra_ = 0U;
        ddrb_ = 0U;
        ira_latched_ = 0xFFU;
        irb_latched_ = 0xFFU;
        t1_ = timer1{};
        t2_ = timer2{};
        sr_ = shifter{};
        acr_ = 0U;
        pcr_ = 0U;
        ifr_ = 0U;
        ier_ = 0U;
        ca1_prev_ = true;
        cb1_prev_ = true;
        cb2_prev_ = true;
        irq_out_ = false;
    }

    std::uint8_t via_6522::composite_ifr() const noexcept {
        const bool active = (ifr_ & ier_ & irq_src_mask) != 0U;
        return static_cast<std::uint8_t>((ifr_ & irq_src_mask) | (active ? irq_master : 0U));
    }

    void via_6522::publish_irq() {
        const bool level = (ifr_ & ier_ & irq_src_mask) != 0U;
        if (level != irq_out_) {
            irq_out_ = level;
            if (cfg_.irq_edge) {
                cfg_.irq_edge(level);
            }
        }
    }

    void via_6522::set_ifr(std::uint8_t bits) {
        ifr_ = static_cast<std::uint8_t>(ifr_ | (bits & irq_src_mask));
        publish_irq();
    }

    void via_6522::clear_ifr(std::uint8_t bits) {
        ifr_ = static_cast<std::uint8_t>(ifr_ & ~(bits & irq_src_mask));
        publish_irq();
    }

    std::uint8_t via_6522::read_pa_live() const {
        if ((acr_ & acr_pa_latch) != 0U) {
            return ira_latched_;
        }
        const std::uint8_t pins = cfg_.read_port_a ? cfg_.read_port_a() : 0xFFU;
        return static_cast<std::uint8_t>((ora_ & ddra_) |
                                         (pins & static_cast<std::uint8_t>(~ddra_)));
    }

    std::uint8_t via_6522::read_pb_live() const {
        std::uint8_t value;
        if ((acr_ & acr_pb_latch) != 0U) {
            value = irb_latched_;
        } else {
            const std::uint8_t pins = cfg_.read_port_b ? cfg_.read_port_b() : 0xFFU;
            value = static_cast<std::uint8_t>((orb_ & ddrb_) |
                                              (pins & static_cast<std::uint8_t>(~ddrb_)));
        }
        if ((acr_ & acr_t1_pb7) != 0U) { // PB7 reports the T1 toggle, ignoring DDR/ORB
            value = static_cast<std::uint8_t>(value & ~pb7_bit);
            if (t1_.pb7_level) {
                value = static_cast<std::uint8_t>(value | pb7_bit);
            }
        }
        return value;
    }

    std::uint8_t via_6522::port_a_pins() const { return read_pa_live(); }
    std::uint8_t via_6522::port_b_pins() const { return read_pb_live(); }

    void via_6522::publish_port_a() {
        if (cfg_.write_port_a) {
            cfg_.write_port_a(port_a_pins());
        }
    }

    void via_6522::publish_port_b() {
        if (cfg_.write_port_b) {
            cfg_.write_port_b(port_b_pins());
        }
    }

    std::uint8_t via_6522::sr_mode() const noexcept {
        return static_cast<std::uint8_t>((acr_ & acr_sr_mask) >> 2U);
    }

    std::uint8_t via_6522::read(std::uint8_t address) {
        const std::uint8_t reg = static_cast<std::uint8_t>(address & 0x0FU);
        switch (reg) {
        case reg_orb:
            clear_ifr(static_cast<std::uint8_t>(irq_cb1 | irq_cb2));
            return read_pb_live();
        case reg_ora:
            clear_ifr(static_cast<std::uint8_t>(irq_ca1 | irq_ca2));
            return read_pa_live();
        case reg_ddrb:
            return ddrb_;
        case reg_ddra:
            return ddra_;
        case reg_t1cl:
            clear_ifr(irq_t1);
            return static_cast<std::uint8_t>(t1_.counter & 0xFFU);
        case reg_t1ch:
            return static_cast<std::uint8_t>((t1_.counter >> 8U) & 0xFFU);
        case reg_t1ll:
            return static_cast<std::uint8_t>(t1_.latch & 0xFFU);
        case reg_t1lh:
            return static_cast<std::uint8_t>((t1_.latch >> 8U) & 0xFFU);
        case reg_t2cl:
            clear_ifr(irq_t2);
            return static_cast<std::uint8_t>(t2_.counter & 0xFFU);
        case reg_t2ch:
            return static_cast<std::uint8_t>((t2_.counter >> 8U) & 0xFFU);
        case reg_sr:
            clear_ifr(irq_sr);
            sr_.count = 8U; // a read rearms an 8-bit transfer
            sr_.active = sr_mode() != 0U;
            return sr_.value;
        case reg_acr:
            return acr_;
        case reg_pcr:
            return pcr_;
        case reg_ifr:
            return composite_ifr();
        case reg_ier:
            return static_cast<std::uint8_t>(ier_ | irq_master);
        case reg_ora_noh:
        default:
            return read_pa_live(); // no IFR side effect
        }
    }

    void via_6522::write(std::uint8_t address, std::uint8_t value) {
        const std::uint8_t reg = static_cast<std::uint8_t>(address & 0x0FU);
        switch (reg) {
        case reg_orb:
            orb_ = value;
            clear_ifr(static_cast<std::uint8_t>(irq_cb1 | irq_cb2));
            publish_port_b();
            return;
        case reg_ora:
            ora_ = value;
            clear_ifr(static_cast<std::uint8_t>(irq_ca1 | irq_ca2));
            publish_port_a();
            return;
        case reg_ddrb:
            ddrb_ = value;
            publish_port_b();
            return;
        case reg_ddra:
            ddra_ = value;
            publish_port_a();
            return;
        case reg_t1cl:
        case reg_t1ll:
            t1_.latch = static_cast<std::uint16_t>((t1_.latch & 0xFF00U) | value);
            return;
        case reg_t1ch:
            t1_.latch = static_cast<std::uint16_t>((t1_.latch & 0x00FFU) | (value << 8U));
            t1_.counter = t1_.latch;
            t1_.underflowed_once = false;
            t1_.reload_phase = false;
            clear_ifr(irq_t1);
            if ((acr_ & acr_t1_pb7) != 0U) {
                t1_.pb7_level = false; // PB7 goes low while T1 runs
                publish_port_b();
            }
            return;
        case reg_t1lh:
            t1_.latch = static_cast<std::uint16_t>((t1_.latch & 0x00FFU) | (value << 8U));
            clear_ifr(irq_t1);
            return;
        case reg_t2cl:
            t2_.latch_low = value;
            return;
        case reg_t2ch:
            t2_.counter = static_cast<std::uint16_t>((value << 8U) | t2_.latch_low);
            t2_.underflowed_once = false;
            clear_ifr(irq_t2);
            return;
        case reg_sr:
            sr_.value = value;
            sr_.count = 8U;
            sr_.phase = 0U;
            sr_.active = sr_mode() != 0U;
            return;
        case reg_acr:
            acr_ = value;
            sr_.active = sr_mode() != 0U && sr_.count > 0U;
            return;
        case reg_pcr:
            pcr_ = value;
            return;
        case reg_ifr:
            clear_ifr(static_cast<std::uint8_t>(value & irq_src_mask));
            return;
        case reg_ier:
            if ((value & irq_master) != 0U) {
                ier_ = static_cast<std::uint8_t>(ier_ | (value & irq_src_mask));
            } else {
                ier_ = static_cast<std::uint8_t>(ier_ & ~(value & irq_src_mask));
            }
            publish_irq();
            return;
        case reg_ora_noh:
        default:
            ora_ = value;
            publish_port_a();
            return;
        }
    }

    void via_6522::t1_step() {
        if (t1_.reload_phase) {
            t1_.counter = t1_.latch; // reload one cycle after the underflow
            t1_.reload_phase = false;
            return;
        }
        if (t1_.counter == 0U) {
            if ((acr_ & acr_t1_cont) != 0U) {
                set_ifr(irq_t1);
                if ((acr_ & acr_t1_pb7) != 0U) {
                    t1_.pb7_level = !t1_.pb7_level;
                    publish_port_b();
                }
                t1_.counter = 0xFFFFU;
                t1_.reload_phase = true;
            } else {
                if (!t1_.underflowed_once) {
                    set_ifr(irq_t1);
                    t1_.underflowed_once = true;
                    if ((acr_ & acr_t1_pb7) != 0U) {
                        t1_.pb7_level = true; // one-shot drives PB7 high on timeout
                        publish_port_b();
                    }
                }
                t1_.counter = 0xFFFFU;
            }
            return;
        }
        --t1_.counter;
    }

    void via_6522::t2_step() {
        if ((acr_ & acr_t2_pulse) != 0U) {
            return; // pulse-count mode decrements only on PB6 edges
        }
        if (t2_.counter == 0U) {
            if (!t2_.underflowed_once) {
                set_ifr(irq_t2);
                t2_.underflowed_once = true;
            }
            t2_.counter = 0xFFFFU;
            return;
        }
        --t2_.counter;
    }

    void via_6522::sr_step() {
        if (!sr_.active || sr_mode() == 0U) {
            return;
        }
        const std::uint8_t mode = sr_mode();
        const bool external = (mode == 3U || mode == 7U);
        if (external) {
            return; // clocked by CB1 edges, not phi2
        }
        // T2-clocked modes (1 and 5) shift every (T2 low latch + 2) cycles; the
        // phi2 modes (2 and 6) shift every 2 cycles.
        const bool t2_clocked = (mode == 1U || mode == 5U);
        const std::uint16_t period =
            t2_clocked ? static_cast<std::uint16_t>(t2_.latch_low + 2U) : 2U;
        ++sr_.phase;
        if (sr_.phase < period) {
            return;
        }
        sr_.phase = 0U;
        if (sr_.count > 0U) {
            --sr_.count;
        }
        if (sr_.count == 0U) {
            set_ifr(irq_sr);
            if (mode == 4U) { // free-running output: rearm immediately
                sr_.count = 8U;
            } else {
                sr_.active = false;
            }
        }
    }

    void via_6522::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            t1_step();
            t2_step();
            sr_step();
        }
    }

    void via_6522::ca1_edge(bool level) {
        const bool active_high = (pcr_ & pcr_ca1_pol) != 0U;
        const bool active = active_high ? (level && !ca1_prev_) : (!level && ca1_prev_);
        ca1_prev_ = level;
        if (active) {
            if ((acr_ & acr_pa_latch) != 0U) {
                ira_latched_ = cfg_.read_port_a ? cfg_.read_port_a() : 0xFFU;
            }
            set_ifr(irq_ca1);
        }
    }

    void via_6522::cb1_edge(bool level) {
        const bool active_high = (pcr_ & pcr_cb1_pol) != 0U;
        const bool active = active_high ? (level && !cb1_prev_) : (!level && cb1_prev_);
        cb1_prev_ = level;
        if (active) {
            if ((acr_ & acr_pb_latch) != 0U) {
                irb_latched_ = cfg_.read_port_b ? cfg_.read_port_b() : 0xFFU;
            }
            set_ifr(irq_cb1);
            // External shift modes clock on the CB1 active edge.
            const std::uint8_t mode = sr_mode();
            if ((mode == 3U || mode == 7U) && sr_.active && sr_.count > 0U) {
                --sr_.count;
                if (sr_.count == 0U) {
                    set_ifr(irq_sr);
                    sr_.active = false;
                }
            }
        }
    }

    void via_6522::cb2_edge(bool level) {
        // Input edge sense from PCR[7:5]; output modes are not modelled.
        const bool active = !level && cb2_prev_; // default negative-edge
        cb2_prev_ = level;
        if (active) {
            set_ifr(irq_cb2);
        }
    }

    void via_6522::pb6_pulse() {
        if ((acr_ & acr_t2_pulse) == 0U) {
            return;
        }
        if (t2_.counter == 0U) {
            if (!t2_.underflowed_once) {
                set_ifr(irq_t2);
                t2_.underflowed_once = true;
            }
            t2_.counter = 0xFFFFU;
            return;
        }
        --t2_.counter;
    }

    bool via_6522::irq_asserted() const noexcept { return irq_out_; }

    void via_6522::save_state(state_writer& writer) const {
        writer.u8(ora_);
        writer.u8(orb_);
        writer.u8(ddra_);
        writer.u8(ddrb_);
        writer.u8(ira_latched_);
        writer.u8(irb_latched_);
        writer.u16(t1_.counter);
        writer.u16(t1_.latch);
        writer.boolean(t1_.pb7_level);
        writer.boolean(t1_.reload_phase);
        writer.boolean(t1_.underflowed_once);
        writer.u16(t2_.counter);
        writer.u8(t2_.latch_low);
        writer.boolean(t2_.underflowed_once);
        writer.u8(sr_.value);
        writer.u8(sr_.count);
        writer.u8(sr_.phase);
        writer.boolean(sr_.active);
        writer.u8(acr_);
        writer.u8(pcr_);
        writer.u8(ifr_);
        writer.u8(ier_);
        writer.boolean(ca1_prev_);
        writer.boolean(cb1_prev_);
        writer.boolean(cb2_prev_);
        writer.boolean(irq_out_);
    }

    void via_6522::load_state(state_reader& reader) {
        ora_ = reader.u8();
        orb_ = reader.u8();
        ddra_ = reader.u8();
        ddrb_ = reader.u8();
        ira_latched_ = reader.u8();
        irb_latched_ = reader.u8();
        t1_.counter = reader.u16();
        t1_.latch = reader.u16();
        t1_.pb7_level = reader.boolean();
        t1_.reload_phase = reader.boolean();
        t1_.underflowed_once = reader.boolean();
        t2_.counter = reader.u16();
        t2_.latch_low = reader.u8();
        t2_.underflowed_once = reader.boolean();
        sr_.value = reader.u8();
        sr_.count = reader.u8();
        sr_.phase = reader.u8();
        sr_.active = reader.boolean();
        acr_ = reader.u8();
        pcr_ = reader.u8();
        ifr_ = reader.u8();
        ier_ = reader.u8();
        ca1_prev_ = reader.boolean();
        cb1_prev_ = reader.boolean();
        cb2_prev_ = reader.boolean();
        irq_out_ = reader.boolean();
    }

    instrumentation::i_chip_introspection& via_6522::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> via_6522::register_snapshot() noexcept {
        register_view_[0] = {"T1", t1_.counter, 16U, register_value_format::unsigned_integer};
        register_view_[1] = {"T2", t2_.counter, 16U, register_value_format::unsigned_integer};
        register_view_[2] = {"IFR", ifr_, 8U, register_value_format::flags};
        register_view_[3] = {"IER", ier_, 8U, register_value_format::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto via_6522_registration = register_factory(
            "mos.6522", chip_class::bus_controller,
            []() -> std::unique_ptr<i_chip> { return std::make_unique<via_6522>(); });
    } // namespace

} // namespace mnemos::chips::bus_controller
