#include "tc0140syt.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::bus_controller {

    tc0140syt::tc0140syt() {
        introspection_.with_registers([this] { return register_snapshot(); });
        reset(reset_kind::power_on);
    }

    chip_metadata tc0140syt::metadata() const noexcept {
        return {.manufacturer = "Taito",
                .part_number = "TC0140SYT",
                .family = "sound communication latch",
                .klass = chip_class::bus_controller,
                .revision = 1U};
    }

    void tc0140syt::tick(std::uint64_t /*cycles*/) {}

    void tc0140syt::reset(reset_kind /*kind*/) {
        command_latch_.fill(0xFFU);
        reply_latch_.fill(0xFFU);
        command_pending_.fill(false);
        reply_pending_.fill(false);
        main_port_ = 0U;
        sound_port_ = 0U;
        main_read_high_ = false;
        main_write_high_ = false;
        sound_read_high_ = false;
        sound_write_high_ = false;
        command_nmi_pulses_ = 0U;
        command_write_count_.fill(0U);
        command_read_count_.fill(0U);
        reply_write_count_.fill(0U);
        reply_read_count_.fill(0U);
        clear_count_ = 0U;
        last_command_write_port_ = 0U;
        last_command_write_value_ = 0xFFU;
        last_command_read_port_ = 0U;
        last_command_read_value_ = 0xFFU;
        last_reply_write_port_ = 0U;
        last_reply_write_value_ = 0xFFU;
        last_reply_read_port_ = 0U;
        last_reply_read_value_ = 0xFFU;
    }

    std::uint8_t& tc0140syt::command_latch(unsigned port) noexcept {
        return command_latch_[port_index(port)];
    }

    const std::uint8_t& tc0140syt::command_latch(unsigned port) const noexcept {
        return command_latch_[port_index(port)];
    }

    std::uint8_t& tc0140syt::reply_latch(unsigned port) noexcept {
        return reply_latch_[port_index(port)];
    }

    const std::uint8_t& tc0140syt::reply_latch(unsigned port) const noexcept {
        return reply_latch_[port_index(port)];
    }

    bool& tc0140syt::command_pending(unsigned port) noexcept {
        return command_pending_[port_index(port)];
    }

    const bool& tc0140syt::command_pending(unsigned port) const noexcept {
        return command_pending_[port_index(port)];
    }

    bool& tc0140syt::reply_pending(unsigned port) noexcept {
        return reply_pending_[port_index(port)];
    }

    const bool& tc0140syt::reply_pending(unsigned port) const noexcept {
        return reply_pending_[port_index(port)];
    }

    std::uint8_t tc0140syt::command_pending_mask() const noexcept {
        return static_cast<std::uint8_t>((command_pending_[0] ? 0x01U : 0U) |
                                         (command_pending_[1] ? 0x02U : 0U));
    }

    std::uint8_t tc0140syt::reply_pending_mask() const noexcept {
        return static_cast<std::uint8_t>((reply_pending_[0] ? 0x01U : 0U) |
                                         (reply_pending_[1] ? 0x02U : 0U));
    }

    std::uint8_t tc0140syt::status() const noexcept {
        return static_cast<std::uint8_t>(command_pending_mask() |
                                         (reply_pending_mask() << 2U));
    }

    std::uint8_t tc0140syt::phase_mask() const noexcept {
        return static_cast<std::uint8_t>((main_read_high_ ? 0x01U : 0U) |
                                         (main_write_high_ ? 0x02U : 0U) |
                                         (sound_read_high_ ? 0x04U : 0U) |
                                         (sound_write_high_ ? 0x08U : 0U));
    }

    void tc0140syt::clear_all_pending() noexcept {
        command_pending_.fill(false);
        reply_pending_.fill(false);
        ++clear_count_;
    }

    void tc0140syt::note_command_write(unsigned port, std::uint8_t value) noexcept {
        const std::size_t index = port_index(port);
        ++command_write_count_[index];
        last_command_write_port_ = static_cast<std::uint8_t>(index);
        last_command_write_value_ = value;
    }

    void tc0140syt::note_command_read(unsigned port, std::uint8_t value) noexcept {
        const std::size_t index = port_index(port);
        ++command_read_count_[index];
        last_command_read_port_ = static_cast<std::uint8_t>(index);
        last_command_read_value_ = value;
    }

    void tc0140syt::note_reply_write(unsigned port, std::uint8_t value) noexcept {
        const std::size_t index = port_index(port);
        ++reply_write_count_[index];
        last_reply_write_port_ = static_cast<std::uint8_t>(index);
        last_reply_write_value_ = value;
    }

    void tc0140syt::note_reply_read(unsigned port, std::uint8_t value) noexcept {
        const std::size_t index = port_index(port);
        ++reply_read_count_[index];
        last_reply_read_port_ = static_cast<std::uint8_t>(index);
        last_reply_read_value_ = value;
    }

    void tc0140syt::save_state(state_writer& writer) const {
        for (const std::uint8_t latch : command_latch_) {
            writer.u8(latch);
        }
        for (const std::uint8_t latch : reply_latch_) {
            writer.u8(latch);
        }
        for (const bool pending : command_pending_) {
            writer.boolean(pending);
        }
        for (const bool pending : reply_pending_) {
            writer.boolean(pending);
        }
        writer.u8(main_port_);
        writer.u8(sound_port_);
        writer.boolean(main_read_high_);
        writer.boolean(main_write_high_);
        writer.boolean(sound_read_high_);
        writer.boolean(sound_write_high_);
        writer.u64(command_nmi_pulses_);
        for (const std::uint64_t count : command_write_count_) {
            writer.u64(count);
        }
        for (const std::uint64_t count : command_read_count_) {
            writer.u64(count);
        }
        for (const std::uint64_t count : reply_write_count_) {
            writer.u64(count);
        }
        for (const std::uint64_t count : reply_read_count_) {
            writer.u64(count);
        }
        writer.u64(clear_count_);
        writer.u8(last_command_write_port_);
        writer.u8(last_command_write_value_);
        writer.u8(last_command_read_port_);
        writer.u8(last_command_read_value_);
        writer.u8(last_reply_write_port_);
        writer.u8(last_reply_write_value_);
        writer.u8(last_reply_read_port_);
        writer.u8(last_reply_read_value_);
    }

    void tc0140syt::load_state(state_reader& reader) {
        for (std::uint8_t& latch : command_latch_) {
            latch = reader.u8();
        }
        for (std::uint8_t& latch : reply_latch_) {
            latch = reader.u8();
        }
        for (bool& pending : command_pending_) {
            pending = reader.boolean();
        }
        for (bool& pending : reply_pending_) {
            pending = reader.boolean();
        }
        main_port_ = reader.u8();
        sound_port_ = reader.u8();
        main_read_high_ = reader.boolean();
        main_write_high_ = reader.boolean();
        sound_read_high_ = reader.boolean();
        sound_write_high_ = reader.boolean();
        command_nmi_pulses_ = reader.u64();
        for (std::uint64_t& count : command_write_count_) {
            count = reader.u64();
        }
        for (std::uint64_t& count : command_read_count_) {
            count = reader.u64();
        }
        for (std::uint64_t& count : reply_write_count_) {
            count = reader.u64();
        }
        for (std::uint64_t& count : reply_read_count_) {
            count = reader.u64();
        }
        clear_count_ = reader.u64();
        last_command_write_port_ = reader.u8();
        last_command_write_value_ = reader.u8();
        last_command_read_port_ = reader.u8();
        last_command_read_value_ = reader.u8();
        last_reply_write_port_ = reader.u8();
        last_reply_write_value_ = reader.u8();
        last_reply_read_port_ = reader.u8();
        last_reply_read_value_ = reader.u8();
    }

    std::span<const register_descriptor> tc0140syt::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"MPORT", main_port_, 4U, fmt::unsigned_integer};
        register_view_[1] = {"SPORT", sound_port_, 4U, fmt::unsigned_integer};
        register_view_[2] = {"M2S0", command_latch_[0], 8U, fmt::unsigned_integer};
        register_view_[3] = {"S2M0", reply_latch_[0], 8U, fmt::unsigned_integer};
        register_view_[4] = {"M2S1", command_latch_[1], 8U, fmt::unsigned_integer};
        register_view_[5] = {"S2M1", reply_latch_[1], 8U, fmt::unsigned_integer};
        register_view_[6] = {"STATUS", status(), 4U, fmt::flags};
        register_view_[7] = {"M2SPEND", command_pending_mask(), 2U, fmt::flags};
        register_view_[8] = {"S2MPEND", reply_pending_mask(), 2U, fmt::flags};
        register_view_[9] = {"MRDPH", main_read_high_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[10] = {"MWRPH", main_write_high_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[11] = {"SRDPH", sound_read_high_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[12] = {"SWRPH", sound_write_high_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[13] = {"CMDNMI", command_nmi_pulses_, 64U, fmt::unsigned_integer};
        register_view_[14] = {"CMDW0", command_write_count_[0], 64U, fmt::unsigned_integer};
        register_view_[15] = {"CMDW1", command_write_count_[1], 64U, fmt::unsigned_integer};
        register_view_[16] = {"CMDR0", command_read_count_[0], 64U, fmt::unsigned_integer};
        register_view_[17] = {"CMDR1", command_read_count_[1], 64U, fmt::unsigned_integer};
        register_view_[18] = {"RPLW0", reply_write_count_[0], 64U, fmt::unsigned_integer};
        register_view_[19] = {"RPLW1", reply_write_count_[1], 64U, fmt::unsigned_integer};
        register_view_[20] = {"RPLR0", reply_read_count_[0], 64U, fmt::unsigned_integer};
        register_view_[21] = {"RPLR1", reply_read_count_[1], 64U, fmt::unsigned_integer};
        register_view_[22] = {"CLEAR", clear_count_, 64U, fmt::unsigned_integer};
        register_view_[23] = {"LCMDP", last_command_write_port_, 1U, fmt::unsigned_integer};
        register_view_[24] = {"LCMD", last_command_write_value_, 8U, fmt::unsigned_integer};
        register_view_[25] = {"LCMRP", last_command_read_port_, 1U, fmt::unsigned_integer};
        register_view_[26] = {"LCMR", last_command_read_value_, 8U, fmt::unsigned_integer};
        register_view_[27] = {"LRPWP", last_reply_write_port_, 1U, fmt::unsigned_integer};
        register_view_[28] = {"LRPW", last_reply_write_value_, 8U, fmt::unsigned_integer};
        register_view_[29] = {"LRPRP", last_reply_read_port_, 1U, fmt::unsigned_integer};
        register_view_[30] = {"LRPR", last_reply_read_value_, 8U, fmt::unsigned_integer};
        return register_view_;
    }

    instrumentation::ichip_introspection& tc0140syt::introspection() noexcept {
        return introspection_;
    }

    namespace {
        [[maybe_unused]] const auto tc0140syt_registration = register_factory(
            "taito.tc0140syt", chip_class::bus_controller,
            []() -> std::unique_ptr<ichip> { return std::make_unique<tc0140syt>(); });
    } // namespace

} // namespace mnemos::chips::bus_controller
