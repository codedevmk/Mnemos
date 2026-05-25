#include <mnemos/chips/storage/c1541/full_drive.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <memory>

namespace mnemos::chips::storage::c1541 {

    full_drive::full_drive(std::uint8_t device_id)
        : device_(static_cast<std::uint8_t>(device_id & 0x0FU)) {
        configure_vias();
    }

    chip_metadata full_drive::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "1541",
            .family = "1541",
            .klass = chip_class::storage,
            .revision = 2U, // 2 = cycle-accurate (synthetic_drive is revision 1)
        };
    }

    void full_drive::configure_vias() {
        cpu_.set_port_enabled(false); // the 1541 CPU is a plain 6502, no $00/$01 port
        cpu_.attach_bus(bus_);

        bus_controller::via_6522::config v1;
        v1.read_port_b = [this]() { return via1_iec_input(); };
        v1.write_port_b = [this](std::uint8_t) { refresh_via1_bus(); };
        v1.irq_edge = [this](bool) { update_cpu_irq(); };
        via1_.configure(v1);

        bus_controller::via_6522::config v2;
        v2.read_port_a = [this]() { return latched_byte_; };
        v2.read_port_b = [this]() { return via2_status_input(); };
        v2.write_port_b = [this](std::uint8_t) { update_mechanism(); };
        v2.irq_edge = [this](bool) { update_cpu_irq(); };
        via2_.configure(v2);
    }

    void full_drive::reset(reset_kind kind) {
        via1_.reset(kind);
        via2_.reset(kind);
        ram_.fill(0U);
        head_half_track_ = park_half_track;
        byte_index_ = 0U;
        latched_byte_ = 0x55U;
        stepper_prev_ = 0U;
        density_zone_ = 3U;
        motor_ = false;
        atn_prev_ = true;
        byte_cycles_ = 0U;
        ratio_acc_ = 0U;
        if (iec_ != nullptr) {
            iec_->release_all(device_);
        }
        cpu_.reset(kind); // loads the DOS ROM reset vector (if a ROM is present)
    }

    void full_drive::attach_bus(iec_bus& bus) noexcept { iec_ = &bus; }

    bool full_drive::load_rom(std::span<const std::uint8_t> rom) {
        if (rom.size() != rom_size) {
            return false;
        }
        rom_.assign(rom.begin(), rom.end());
        return true;
    }

    bool full_drive::mount(std::span<const std::uint8_t> d64) {
        if (!disk_.load(d64)) {
            return false;
        }
        tracks_ = bind_gcr(disk_);
        return true;
    }

    void full_drive::set_clock_ratio(std::uint32_t drive_hz, std::uint32_t host_hz) noexcept {
        ratio_num_ = drive_hz != 0U ? drive_hz : 1U;
        ratio_den_ = host_hz != 0U ? host_hz : 1U;
    }

    std::uint8_t full_drive::bus_read(std::uint16_t address) {
        if (address < 0x1800U) {
            return ram_[address & 0x7FFU];
        }
        if (address < 0x1C00U) {
            return via1_.read(static_cast<std::uint8_t>(address & 0x0FU));
        }
        if (address < 0x2000U) {
            return via2_.read(static_cast<std::uint8_t>(address & 0x0FU));
        }
        if (address >= 0xC000U && rom_.size() == rom_size) {
            return rom_[address & 0x3FFFU];
        }
        return 0xFFU; // open bus
    }

    void full_drive::bus_write(std::uint16_t address, std::uint8_t value) {
        if (address < 0x1800U) {
            ram_[address & 0x7FFU] = value;
            return;
        }
        if (address < 0x1C00U) {
            via1_.write(static_cast<std::uint8_t>(address & 0x0FU), value);
            return;
        }
        if (address < 0x2000U) {
            via2_.write(static_cast<std::uint8_t>(address & 0x0FU), value);
            return;
        }
        // ROM / open bus: writes are dropped.
    }

    std::uint8_t full_drive::via1_iec_input() const {
        std::uint8_t v = static_cast<std::uint8_t>(((device_ - 8U) & 0x03U) << 5U); // PB5/6 jumpers
        if (iec_ != nullptr) {
            if (iec_->asserted(iec_bus::line::data)) {
                v = static_cast<std::uint8_t>(v | 0x01U); // PB0 DATA in (7406: low -> 1)
            }
            if (iec_->asserted(iec_bus::line::clk)) {
                v = static_cast<std::uint8_t>(v | 0x04U); // PB2 CLK in
            }
            if (iec_->asserted(iec_bus::line::atn)) {
                v = static_cast<std::uint8_t>(v | 0x80U); // PB7 ATN in
            }
        }
        return v;
    }

    void full_drive::refresh_via1_bus() {
        if (iec_ == nullptr) {
            return;
        }
        const std::uint8_t out = via1_.port_b_output();
        iec_->set_driver(device_, iec_bus::line::clk, (out & 0x08U) != 0U); // PB3 CLK out
        // DATA out (PB1) OR the auto-ATN-ack gate: ATN_in XOR PB4.
        const bool atn_in = iec_->asserted(iec_bus::line::atn);
        const bool pb4 = (out & 0x10U) != 0U;
        const bool data_low = ((out & 0x02U) != 0U) || (atn_in != pb4);
        iec_->set_driver(device_, iec_bus::line::data, data_low);
    }

    void full_drive::poll_atn() {
        if (iec_ == nullptr) {
            return;
        }
        const bool atn = iec_->asserted(iec_bus::line::atn);
        if (atn != atn_prev_) {
            via1_.ca1_edge(atn); // CA1 follows the ATN chip pin
            atn_prev_ = atn;
            refresh_via1_bus(); // the auto-ack gate depends on ATN
        }
    }

    std::uint8_t full_drive::via2_status_input() const {
        std::uint8_t v = 0x10U; // PB4 = 1: not write-protected
        const bool sync = motor_ && latched_byte_ == 0xFFU;
        if (!sync) {
            v = static_cast<std::uint8_t>(v | 0x80U); // PB7 high = no SYNC under head
        }
        return v;
    }

    void full_drive::update_mechanism() {
        const std::uint8_t out = via2_.port_b_output();
        const std::uint8_t stepper = static_cast<std::uint8_t>(out & 0x03U);
        const std::uint8_t delta = static_cast<std::uint8_t>((stepper - stepper_prev_) & 0x03U);
        if (delta == 1U && head_half_track_ < 83U) {
            ++head_half_track_; // outward (toward higher tracks)
        } else if (delta == 3U && head_half_track_ > 0U) {
            --head_half_track_; // inward
        }
        stepper_prev_ = stepper;
        motor_ = (out & 0x04U) != 0U;
        density_zone_ = static_cast<std::uint8_t>((out >> 5U) & 0x03U);
    }

    void full_drive::update_cpu_irq() {
        cpu_.set_irq_line(via1_.irq_asserted() || via2_.irq_asserted());
    }

    void full_drive::advance_head() {
        if (tracks_.empty()) {
            return;
        }
        const gcr_track& track = tracks_[(current_track() - 1U) % tracks_.size()];
        if (track.bytes.empty()) {
            return;
        }
        byte_index_ = (byte_index_ + 1U) % track.bytes.size();
        latched_byte_ = track.bytes[byte_index_];
        if (latched_byte_ != 0xFFU) { // a real (non-SYNC) byte is ready
            via2_.ca1_edge(false);
            via2_.ca1_edge(true); // byte-ready edge -> VIA2 CA1 IRQ
            cpu_.set_flag(cpu::m6510::status_flag::overflow, true); // SO pin -> the BVC loop
            for (int i = 0; i < 8; ++i) {
                via2_.pb6_pulse(); // clock the byte into VIA2 T2 (pulse count)
            }
        }
    }

    void full_drive::drive_cycle() {
        poll_atn();
        via1_.tick(1U);
        via2_.tick(1U);
        cpu_.tick(1U);
        if (motor_ && !tracks_.empty()) {
            if (++byte_cycles_ >= gcr_zone_byte_period[density_zone_]) {
                byte_cycles_ = 0U;
                advance_head();
            }
        }
    }

    void full_drive::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ratio_acc_ += ratio_num_;
            while (ratio_acc_ >= ratio_den_) {
                ratio_acc_ -= ratio_den_;
                drive_cycle();
            }
        }
    }

    void full_drive::save_state(state_writer& writer) const {
        cpu_.save_state(writer);
        via1_.save_state(writer);
        via2_.save_state(writer);
        writer.bytes(std::span<const std::uint8_t>(ram_));
        writer.u8(device_);
        writer.u8(head_half_track_);
        writer.u64(byte_index_);
        writer.u8(latched_byte_);
        writer.u8(stepper_prev_);
        writer.u8(density_zone_);
        writer.boolean(motor_);
        writer.boolean(atn_prev_);
        writer.u32(byte_cycles_);
        writer.u64(ratio_acc_);
    }

    void full_drive::load_state(state_reader& reader) {
        cpu_.load_state(reader);
        via1_.load_state(reader);
        via2_.load_state(reader);
        reader.bytes(std::span<std::uint8_t>(ram_));
        device_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        head_half_track_ = reader.u8();
        byte_index_ = reader.u64();
        latched_byte_ = reader.u8();
        stepper_prev_ = reader.u8();
        density_zone_ = reader.u8();
        motor_ = reader.boolean();
        atn_prev_ = reader.boolean();
        byte_cycles_ = reader.u32();
        ratio_acc_ = reader.u64();
    }

    instrumentation::i_chip_introspection& full_drive::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto full_drive_registration = register_factory(
            "commodore.c1541.full", chip_class::storage,
            []() -> std::unique_ptr<i_chip> { return std::make_unique<full_drive>(); });
    } // namespace

} // namespace mnemos::chips::storage::c1541
