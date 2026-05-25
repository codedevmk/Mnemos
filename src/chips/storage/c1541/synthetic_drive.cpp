#include "synthetic_drive.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::storage::c1541 {
    namespace {

        constexpr std::uint8_t cmd_listen = 0x20U; // $20 + device
        constexpr std::uint8_t cmd_unlisten = 0x3FU;
        constexpr std::uint8_t cmd_talk = 0x40U; // $40 + device
        constexpr std::uint8_t cmd_untalk = 0x5FU;
        constexpr std::uint8_t cmd_data = 0x60U;  // $60 + secondary address
        constexpr std::uint8_t cmd_close = 0xE0U; // $E0 + secondary address
        constexpr std::uint8_t cmd_open = 0xF0U;  // $F0 + secondary address

    } // namespace

    chip_metadata synthetic_drive::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "1541",
            .family = "1541",
            .klass = chip_class::storage,
            .revision = 1U,
        };
    }

    void synthetic_drive::reset(reset_kind /*kind*/) {
        for (channel& ch : channels_) {
            ch.data.clear();
            ch.pos = 0U;
            ch.open = false;
        }
        open_name_.clear();
        open_sa_ = 0U;
        listen_addressed_ = false;
        talk_addressed_ = false;
        open_pending_ = false;
        listen_sa_ = 0U;
        talk_sa_ = 0U;
        link_ = link_mode::idle;
        serial_phase_ = 0U;
        serial_bit_ = 0U;
        serial_shift_ = 0U;
        atn_prev_ = true;
        clk_prev_ = true;
        under_atn_ = false;
        if (bus_ != nullptr) {
            bus_->release_all(device_);
        }
    }

    bool synthetic_drive::mount(std::span<const std::uint8_t> d64) { return disk_.load(d64); }
    void synthetic_drive::unmount() noexcept { disk_ = d64_image{}; }

    void synthetic_drive::process_command(std::uint8_t command) {
        const std::uint8_t low = static_cast<std::uint8_t>(command & 0x0FU);
        if (command >= cmd_listen && command < cmd_unlisten) { // LISTEN device
            listen_addressed_ = (low == device_);
            return;
        }
        if (command == cmd_unlisten) {
            if (open_pending_) {
                load_into_channel(open_sa_);
                open_pending_ = false;
            }
            listen_addressed_ = false;
            return;
        }
        if (command >= cmd_talk && command < cmd_untalk) { // TALK device
            talk_addressed_ = (low == device_);
            return;
        }
        if (command == cmd_untalk) {
            talk_addressed_ = false;
            return;
        }
        if (command >= cmd_data && command <= 0x6FU) { // channel / secondary address
            if (talk_addressed_) {
                talk_sa_ = low;
                channels_[low].pos = 0U; // restart the talk stream
            }
            if (listen_addressed_) {
                listen_sa_ = low;
            }
            return;
        }
        if (command >= cmd_close && command <= 0xEFU) { // CLOSE channel
            if (listen_addressed_) {
                channels_[low].open = false;
                channels_[low].data.clear();
                channels_[low].pos = 0U;
            }
            return;
        }
        if (command >= cmd_open) { // OPEN file ($F0-$FF)
            if (listen_addressed_) {
                open_pending_ = true;
                open_sa_ = low;
                open_name_.clear();
            }
            return;
        }
    }

    void synthetic_drive::process_listen_payload(std::uint8_t byte) {
        if (open_pending_) {
            open_name_.push_back(byte);
        }
    }

    void synthetic_drive::load_into_channel(std::uint8_t sa) {
        channel& ch = channels_[sa & 0x0FU];
        ch.data.clear();
        ch.pos = 0U;
        ch.open = true;
        if (!disk_.loaded()) {
            return; // no disk: empty channel (file-not-found)
        }

        if (!open_name_.empty() && open_name_[0] == static_cast<std::uint8_t>('$')) {
            ch.data = disk_.render_directory_listing();
            return;
        }

        std::optional<d64_image::dir_entry> entry;
        const bool wildcard_first =
            open_name_.empty() ||
            (open_name_.size() == 1U && open_name_[0] == static_cast<std::uint8_t>('*'));
        if (wildcard_first) {
            entry = disk_.find_first_prg();
        } else {
            entry = disk_.find_by_name(open_name_);
        }
        if (entry) {
            ch.data = disk_.read_chain(entry->first_track, entry->first_sector);
        }
    }

    void synthetic_drive::debug_command(std::uint8_t command) { process_command(command); }
    void synthetic_drive::debug_filename_byte(std::uint8_t byte) { process_listen_payload(byte); }
    void synthetic_drive::debug_unlisten() { process_command(cmd_unlisten); }

    std::optional<std::uint8_t> synthetic_drive::debug_pull_byte() {
        channel& ch = channels_[talk_sa_ & 0x0FU];
        if (ch.pos >= ch.data.size()) {
            return std::nullopt;
        }
        return ch.data[ch.pos++];
    }

    void synthetic_drive::pull(iec_bus::line l, bool low) noexcept {
        if (bus_ != nullptr) {
            bus_->set_driver(device_, l, low);
        }
    }

    bool synthetic_drive::sense(iec_bus::line l) const noexcept {
        return bus_ != nullptr && bus_->asserted(l);
    }

    // Byte-level IEC handshake. This drives the bus the real KERNAL serial routines
    // speak; end-to-end LOAD is validated with the C64 KERNAL ROM (data-gated, like
    // the golden boot). The command/serving logic above is what the debug_* tests
    // exercise directly.
    void synthetic_drive::serial_tick() {
        if (bus_ == nullptr) {
            return;
        }
        const bool atn = sense(iec_bus::line::atn); // true = ATN asserted (low)

        if (atn && !atn_prev_) { // controller asserted ATN: command phase begins
            link_ = link_mode::receiving;
            under_atn_ = true;
            serial_bit_ = 0U;
            serial_shift_ = 0U;
            pull(iec_bus::line::data, true); // acknowledge attention
        } else if (!atn && atn_prev_) {      // ATN released
            if (talk_addressed_) {
                link_ = link_mode::talking;
                serial_bit_ = 0U;
            } else {
                link_ = listen_addressed_ ? link_mode::receiving : link_mode::idle;
                under_atn_ = false;
            }
        }
        atn_prev_ = atn;

        const bool clk = sense(iec_bus::line::clk);
        if (link_ == link_mode::receiving) {
            // Latch a data bit on each rising CLK edge; assemble LSB-first.
            if (clk && !clk_prev_) {
                const bool bit = !sense(iec_bus::line::data); // DATA low on the wire = 1
                serial_shift_ =
                    static_cast<std::uint8_t>(serial_shift_ | (bit ? (1U << serial_bit_) : 0U));
                ++serial_bit_;
                if (serial_bit_ >= 8U) {
                    if (under_atn_) {
                        process_command(serial_shift_);
                    } else {
                        process_listen_payload(serial_shift_);
                    }
                    serial_bit_ = 0U;
                    serial_shift_ = 0U;
                    pull(iec_bus::line::data, true); // byte acknowledge
                }
            }
        }
        clk_prev_ = clk;
    }

    void synthetic_drive::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            serial_tick();
        }
    }

    void synthetic_drive::save_state(state_writer& writer) const {
        writer.u8(device_);
        for (const channel& ch : channels_) {
            writer.blob(ch.data);
            writer.u32(static_cast<std::uint32_t>(ch.pos));
            writer.boolean(ch.open);
        }
        writer.blob(open_name_);
        writer.u8(open_sa_);
        writer.boolean(listen_addressed_);
        writer.boolean(talk_addressed_);
        writer.boolean(open_pending_);
        writer.u8(listen_sa_);
        writer.u8(talk_sa_);
        writer.u8(static_cast<std::uint8_t>(link_));
        writer.u8(serial_phase_);
        writer.u8(serial_bit_);
        writer.u8(serial_shift_);
        writer.boolean(atn_prev_);
        writer.boolean(clk_prev_);
        writer.boolean(under_atn_);
    }

    void synthetic_drive::load_state(state_reader& reader) {
        device_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        for (channel& ch : channels_) {
            ch.data = reader.blob();
            ch.pos = reader.u32();
            ch.open = reader.boolean();
        }
        open_name_ = reader.blob();
        open_sa_ = reader.u8();
        listen_addressed_ = reader.boolean();
        talk_addressed_ = reader.boolean();
        open_pending_ = reader.boolean();
        listen_sa_ = reader.u8();
        talk_sa_ = reader.u8();
        link_ = static_cast<link_mode>(reader.u8());
        serial_phase_ = reader.u8();
        serial_bit_ = reader.u8();
        serial_shift_ = reader.u8();
        atn_prev_ = reader.boolean();
        clk_prev_ = reader.boolean();
        under_atn_ = reader.boolean();
    }

    instrumentation::ichip_introspection& synthetic_drive::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto c1541_registration = register_factory(
            "commodore.c1541", chip_class::storage,
            []() -> std::unique_ptr<ichip> { return std::make_unique<synthetic_drive>(); });
    } // namespace

} // namespace mnemos::chips::storage::c1541
